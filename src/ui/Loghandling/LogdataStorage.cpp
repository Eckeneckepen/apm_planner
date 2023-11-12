/*===================================================================
APM_PLANNER Open Source Ground Control Station

(c) 2016 APM_PLANNER PROJECT <http://www.ardupilot.com>

This file is part of the APM_PLANNER project

    APM_PLANNER is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    APM_PLANNER is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with APM_PLANNER. If not, see <http://www.gnu.org/licenses/>.

======================================================================*/
/**
 * @file LogdataStorage.cpp
 * @author Arne Wischmann <wischmann-a@gmx.de>
 * @date 16 Nov 2016
 * @brief File providing implementation for the data storage for logfiles
 */


#include "LogdataStorage.h"
#include "logging.h"
#include <algorithm>

/**
 * @brief The TimeStampToIndexPairComparer class is a functor for sorting the
 *        time index by time.
 */
class TimeStampToIndexPairComparer
{
public:
    bool operator() (LogdataStorage::TimeStampToIndexPair pair1, LogdataStorage::TimeStampToIndexPair pair2)
    {
        // pair.first is the time stamp
        return (pair1.first < pair2.first);
    }
};

//****************************************************

LogdataStorage::LogdataStorage()
{
    QLOG_DEBUG() << "LogdataStorage::LogdataStorage()";
    // Reserve some memory...
    m_typeStorage.reserve(50);
    m_indexToTypeRow.reserve(50);
    m_dataStorage.reserve(50);
    m_TimeToIndexList.reserve(20000);
    m_indexToDataRow.reserve(20000);

    // add some default data to the multiplier
    m_multiplierStorage[45] = qQNaN();  // invalid / unused multiplier - missing in some logs therefore added here.
    m_multiplierStorage[63] = qQNaN();  // unknown multiplier - missing in some logs therefore added here.
}

LogdataStorage::~LogdataStorage()
{
    QLOG_DEBUG() << "LogdataStorage::~LogdataStorage()";
}

int LogdataStorage::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return m_indexToDataRow.size();
}

int LogdataStorage::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return m_columnCount;
}

QVariant LogdataStorage::data(const QModelIndex &index, int role) const
{
    if ((role != Qt::DisplayRole) || !index.isValid())
    {
        return {};
    }
    if (index.row() >= m_indexToDataRow.size())
    {
        QLOG_ERROR() << "Accessing a row that does not exist! Row was: " << index.row();
        return {};
    }
    if (index.column() == 0)
    {
        // Column 0 is the index of the log data which is the same as the row
        return {QString::number(index.row())};
    }
    if (index.column() == 1)
    {
        // Column 1 is the name of the log data (ATT,ATUN...)
        return {m_indexToDataRow[index.row()].first};
    }

    const TypeIndexPair &typeIndex = m_indexToDataRow[index.row()];
    const ValueTable &dataVect = m_dataStorage[typeIndex.first];
    if(index.column() - s_ColumnOffset >= dataVect.at(typeIndex.second).m_values.size())
    {
        return {}; // this data type does not have so much colums
    }

    const dataType &type = m_typeStorage[typeIndex.first];
    if((index.column() - s_ColumnOffset) < type.m_multipliers.size())   // do we have a multiplier??
    {
        const double &multi = type.m_multipliers.at(index.column() - s_ColumnOffset);
        if(!qIsNaN(multi))     // unknown multiplier are NaNs
        {
            double temp = dataVect.at(typeIndex.second).m_values.at(index.column() - s_ColumnOffset).toDouble();
            if(index.column() == 2)
            {
                // Column 2 is the time we want 6 decimals in this one.
                return QString::number(temp * multi, 'f', 6);
            }
            return {temp * multi};
        }
    }
    // If we do not have multipliers we do not need scaling
    return dataVect.at(typeIndex.second).m_values.at(index.column() - s_ColumnOffset);
}

QVariant LogdataStorage::headerData(int column, Qt::Orientation orientation, int role) const
{
    if ((column == -1) || (role != Qt::DisplayRole) || (orientation == Qt::Vertical))
    {
        return {};
    }
    if (m_indexToDataRow.empty())
    {
        return {"No Data"};  // corner case - we do not have any data
    }
    if (column == 0)
    {
        return {"Index"};   // first colum is always the index
    }
    if (column == 1)
    {
        return {"MSG Type"};    // second colum is always the message type
    }

    const TypeIndexPair &typeIndex = m_indexToDataRow[m_currentRow];
    const dataType &type = m_typeStorage[typeIndex.first];
    if ((column - s_ColumnOffset) >= type.m_labels.size())
    {
        return {""};    // this row does not have this column
    }

    return {getLabelName(column - s_ColumnOffset, type)};
}

bool LogdataStorage::addDataType(const QString &typeName, quint32 typeID, int typeLength,
                                 const QString &typeFormat, const QStringList &typeLabels, int timeColumn)
{
    // set up column count - the storage adds s_ColumnOffset columns to the data.
    // One for the index and one for the name.
    m_columnCount = m_columnCount < (typeLabels.size() + s_ColumnOffset) ? typeLabels.size() + s_ColumnOffset : m_columnCount;

    // create new type and store it
    dataType NewType(typeName, typeID, typeLength, typeFormat, typeLabels, timeColumn);
    m_typeStorage.insert(typeName, NewType);
    // to be able to recreate the order we store the names in a vector.
    m_indexToTypeRow.push_back(typeName);

    return true;
}

bool LogdataStorage::addDataRow(const QString &typeName, const QList<QPair<QString,QVariant> >  &values)
{
    if (!m_typeStorage.contains(typeName))  // type exists in type storage?
    {
        m_errorText.clear();
        QTextStream error(&m_errorText);
        error << "Data of type " << typeName << " cannot be inserted cause its type is unknown.";
        return false;
    }

    const dataType &tempType = m_typeStorage[typeName];
    if(values.size() != tempType.m_labels.size())    // Number of elements match type?
    {
        m_errorText.clear();
        QTextStream error(&m_errorText);
        error << "Number of datafields for type " << typeName << " does not match. Expected:"
              << tempType.m_labels.size() << " got:" << values.size();
        return false;
    }

    // fetch min & max timestamp of all data
    quint64 tempTime = values.at(tempType.m_timeStampIndex).second.toULongLong();
    m_minTimeStamp = m_minTimeStamp > tempTime ? tempTime : m_minTimeStamp;
    m_maxTimeStamp = m_maxTimeStamp < tempTime ? tempTime : m_maxTimeStamp;

    IndexValueRow newRow;
    newRow.m_values.reserve(values.size());
    for(int i = 0; i < values.size(); ++i)
    {
        if(values[i].first != tempType.m_labels[i])  // value name match?
        {
            m_errorText.clear();
            QTextStream error(&m_errorText);
            error << "Data of type " << typeName << " Value name mismatch. Expected:"
                  << tempType.m_labels[i] << " but got:" << values[i].first
                  << " Dropping data.";
            return false;
        }
        newRow.m_values.push_back(values[i].second);
    }
    // add current global dataindex to row
    newRow.m_index = m_indexToDataRow.size();   // size() will be the index after push_back()
    // add to data storage
    m_dataStorage[typeName].append(newRow);
    // add type name to global dataindex
    m_indexToDataRow.push_back(TypeIndexPair(typeName, m_dataStorage[typeName].size() - 1)); // last index is size() - 1
    // create time to index pair
    TimeStampToIndexPair timeIndex(tempTime, newRow.m_index);
    // and add it to time index
    m_TimeToIndexList.push_back(timeIndex);
    return true;
}

void LogdataStorage::addUnitData(quint8 unitID, const QString &unitName)
{
    m_unitStorage[unitID] = unitName;
}

void LogdataStorage::addMultiplierData(quint8 multiID, double multiplier)
{
    m_multiplierStorage[multiID] = multiplier;
}

void LogdataStorage::addMsgToUnitAndMultiplierData(quint32 typeID, const QByteArray &multiplierFieldInfo, const QByteArray &unitFieldInfo)
{
    m_typeIDToMultiplierFieldInfo[typeID] = multiplierFieldInfo;
    m_typeIDToUnitFieldInfo[typeID] = unitFieldInfo;
}

void LogdataStorage::selectedRowChanged(const QModelIndex &current)
{
    if (!current.isValid() || (m_currentRow == current.row()))
    {
        return;
    }
    m_currentRow = current.row() < m_indexToDataRow.size() ? current.row() : m_indexToDataRow.size() - 1; // size() -1 is last index
    emit headerDataChanged(Qt::Horizontal,0,9);
}

void LogdataStorage::setTimeStamp(const QString &timeStampName, double divisor)
{
    m_timeStampName = timeStampName;
    m_timeDivisor = divisor;

    // As this method is called at the End of the parsing we should use the chance to sort the time index by
    // time - just to be sure...
    std::stable_sort(m_TimeToIndexList.begin(), m_TimeToIndexList.end(), TimeStampToIndexPairComparer());
}

double LogdataStorage::getTimeDivisor() const
{
    return m_timeDivisor;
}

QMap<QString, QStringList> LogdataStorage::getFmtValues(bool filterStringValues) const
{
    QMap<QString, QStringList> fmtValueMap; //using a map to get alphabetically sorting

    for(const auto &type : m_typeStorage)
    {
        if(m_dataStorage.contains(type.m_name))    // only types we have data for
        {
            if(!filterStringValues ||           // n N Z are string types - those cannot be plotted
               !(type.m_format.contains('n') || type.m_format.contains('N') || type.m_format.contains('Z')))
            {
                // first create list consisting of labels and their unit
                QStringList labelPlusUnit;
                labelPlusUnit.reserve(type.m_labels.size());
                for(int i = 0; i < type.m_labels.size(); ++i)
                {
                    labelPlusUnit.append(getLabelName(i, type));
                }

                // check if this value has an indexed one
                int indexFieldPos = m_typeIDToUnitFieldInfo.value(type.m_ID).indexOf('#');
                if(indexFieldPos != -1)    // This is an indexed value
                {
                    // if we have an index we remove the field describing the index from the list cause nobody wants to plot it.
                    labelPlusUnit.removeAt(indexFieldPos);
                    // now we create a new typename from the type name the index label and the possible ID
                    QString typeName(type.m_name);
                    typeName.append('.');
                    typeName.append(type.m_labels[indexFieldPos]);
                    typeName.append(':');
                    for (int i = 0; i <= type.m_maxIndex; ++i)
                    {
                        fmtValueMap.insert(typeName + QString::number(i), labelPlusUnit);
                    }
                }
                else
                {
                    // this is an value without an index just store its name and its datafields
                    fmtValueMap.insert(type.m_name, labelPlusUnit);
                }
            }
        }
    }
    return fmtValueMap;
}

QVector<LogdataStorage::dataType> LogdataStorage::getAllDataTypes() const
{
    QVector<dataType> dataTypes;
    dataTypes.reserve(m_indexToTypeRow.size());

    foreach(const QString &name, m_indexToTypeRow)
    {
        dataTypes.push_back(m_typeStorage.value(name));
    }

    return dataTypes;
}


bool LogdataStorage::getValues(const QString &name, bool useTimeAsIndex, QVector<double> &xValues, QVector<double> &yValues) const
{
    // we expect a name like groupName.indexName:idx.valueName or groupName.valueName

    auto splitName = name.split('.');
    // sanity checking
    if((splitName.size() < 2) || (splitName.size() > 3 ))
    {
        return false;   // name is not valid - structure must be "groupName.indexName:idx.valueName or groupName.valueName"
    }
    if(!m_typeStorage.contains(splitName.at(0)) || !m_dataStorage.contains(splitName.at(0)))
    {
        return false;    // don't have this type or no data for this type
    }

    // get the type for easier access
    const auto &type = m_typeStorage[splitName.at(0)];

    // The last element is always the valueName
    auto valueName = splitName.last().split(s_UnitParOpen).at(0).trimmed();  // Remove unit info like "[s]" from valueName

    const int valueIndex = type.m_labels.indexOf(valueName);
    if(valueIndex == -1)
    {
        return false;    // don't have this value type
    }

    const int timeStampIndex {type.m_timeStampIndex};
    double multiplier {qQNaN()};                        // Unknown multiplier is always qQNaN
    if(type.m_multipliers.size() > valueIndex)
    {
        multiplier = type.m_multipliers[valueIndex];
    }

    int reqDataline {};
    bool canHaveMultipleDatalines {false};
    if (splitName.size() == 3)
    {
        // this is an indexed type. The index is splitName[1]. Its like instance:0, instance:1 ...
        const auto indexSplit = splitName[1].split(':');
        reqDataline = indexSplit.at(1).trimmed().toInt();
        canHaveMultipleDatalines = true;
    }

    int datalines {type.m_maxIndex + 1};
    const ValueTable &data {m_dataStorage[splitName.at(0)]};

    xValues.clear();
    xValues.reserve((data.size() / (datalines)) + 2 );  // the +2 is to gurantee the vector is big enough (really no reallocation is needed)
    yValues.clear();
    yValues.reserve((data.size() / (datalines)) + 2 );

    // copy the requested data
    if (canHaveMultipleDatalines && (datalines > 1))    // only if we really have more than one dataline.
    {
        for (const auto &valueRow: data)
        {
            if (valueRow.m_values.at(type.m_indexFieldIndex).toInt() == reqDataline)    // only if its the requested dataline
            {
                xValues.push_back((useTimeAsIndex ? valueRow.m_values.at(timeStampIndex).toDouble() / m_timeDivisor : valueRow.m_index));
                if(!qIsNaN(multiplier))
                {
                    yValues.push_back(valueRow.m_values.at(valueIndex).toDouble() * multiplier);
                }
                else
                {
                    yValues.push_back(valueRow.m_values.at(valueIndex).toDouble());
                }
            }
        }
    }
    else
    {
        for (const auto &valueRow: data)
        {
            xValues.push_back((useTimeAsIndex ? valueRow.m_values.at(timeStampIndex).toDouble() / m_timeDivisor : valueRow.m_index));
            if(!qIsNaN(multiplier))
            {
                yValues.push_back(valueRow.m_values.at(valueIndex).toDouble() * multiplier);
            }
            else
            {
                yValues.push_back(valueRow.m_values.at(valueIndex).toDouble());
            }
        }
    }

    return true;
}

void LogdataStorage::getRawDataRow(int index, QString &name, QVector<QVariant> &measurements) const
{
    if(index < m_indexToDataRow.size())
    {
        TypeIndexPair indexPair = m_indexToDataRow[index];
        name = indexPair.first;
        measurements = m_dataStorage.value(name).at(indexPair.second).m_values;
    }
    else
    {
        name.clear();
        measurements.clear();
    }
}

QHash<quint8, QString> LogdataStorage::getUnitData() const
{
    return m_unitStorage;
}

QHash<quint8, double> LogdataStorage::getMultiplierData() const
{
    return m_multiplierStorage;
}

QPair<QByteArray, QByteArray> LogdataStorage::getMsgToUnitAndMultiplierData(quint32 typeID) const
{
    QPair<QByteArray, QByteArray> dataPair;

    dataPair.first  = m_typeIDToMultiplierFieldInfo[typeID];
    dataPair.second = m_typeIDToUnitFieldInfo[typeID];
    return dataPair;
}

double LogdataStorage::getMinTimeStamp() const
{
    return static_cast<double>(m_minTimeStamp) / m_timeDivisor;
}

double LogdataStorage::getMaxTimeStamp() const
{
    return static_cast<double>(m_maxTimeStamp) / m_timeDivisor;
}

int LogdataStorage::getNearestIndexForTimestamp(double timevalue) const
{
    auto timeToFind   = static_cast<quint64>(m_timeDivisor * timevalue);
    auto intervalSize = static_cast<float>(m_TimeToIndexList.size());
    float intervalStart = 0;
    float middle = 0;
    int   index = 0;

    // first check if timevalue is within our range
    if(m_TimeToIndexList.first().first > timeToFind)
    {
        return 0;   // timevalue too small deliver index 0
    }

    if(m_TimeToIndexList.last().first < timeToFind)
    {
        return m_TimeToIndexList.size();    // timevalue too big deliver last index.
    }

    while (intervalSize > 1)
    {
        middle = intervalStart + intervalSize / 2;
        index = qRound(middle);
        quint64 tempTime = m_TimeToIndexList[index].first;
        if (timeToFind > tempTime)
        {
            intervalStart = middle;
        }
        else if (timeToFind == tempTime)
        {
            break;
        }
        intervalSize = intervalSize / 2;
    }

    return m_TimeToIndexList[index].second;
}

void LogdataStorage::getMessagesOfType(const QString &type, QMap<quint64, MessageBase::Ptr> &indexToMessageMap) const
{
    if(!m_dataStorage.contains(type))
    {
        QLOG_DEBUG() << "Graph loaded with no table of type " << type;
        return;
    }

    QList<NameValuePair> nameValueList;

    for(const auto &row : m_dataStorage[type])
    {
        nameValueList.clear();
        nameValueList.append(NameValuePair("Index", row.m_index));  // Add Data index

        for(int i = 0; i < row.m_values.size(); ++i)
        {
            NameValuePair tempPair(m_typeStorage[type].m_labels.at(i), row.m_values.at(i)); // add names and values
            nameValueList.append(tempPair);
        }
        MessageBase::Ptr msgPtr = MessageFactory::CreateMessageOfType(type, nameValueList, m_timeStampName, m_timeDivisor);
        if(msgPtr != nullptr)
        {
            indexToMessageMap.insert(static_cast<quint64>(row.m_index), msgPtr);
        }
    }
}

QString LogdataStorage::getError() const
{
    return m_errorText;
}

QStringList LogdataStorage::setupUnitData(const QString &timeStampName, double divisor)
{
    QStringList errors;
    static constexpr int s_maxItemsToCheck {50};

    // handle the unit and multiplier data if there is some
    if(!m_typeIDToMultiplierFieldInfo.empty())
    {
        for(auto &type : m_typeStorage)
        {
            // handle unit and index data
            if(m_typeIDToUnitFieldInfo.contains(type.m_ID))
            {
                // handle unit data
                for(const qint8 unitID : m_typeIDToUnitFieldInfo.value(type.m_ID))
                {
                    if(m_unitStorage.contains(static_cast<quint8>(unitID)))
                    {
                        type.m_units.push_back(m_unitStorage.value(static_cast<quint8>(unitID)));
                    }
                    else
                    {
                        QString error("Found unit ID " + QString::number(unitID) + " but no matching unit");
                        errors.append(error);
                    }
                }
                // handle index data
                int indexFieldPos = m_typeIDToUnitFieldInfo.value(type.m_ID).indexOf('#'); // '#' is the unitID for index fields
                if(indexFieldPos != -1)
                {
                    if(m_dataStorage.contains(type.m_name)) // only if we have data
                    {
                        // find the max index within the first 50 entries and store it within the datatype
                        const auto &valueRow {m_dataStorage[type.m_name]};
                        int maxIndex {0};
                        int maxEntriesToCheck {valueRow.size() < s_maxItemsToCheck ? valueRow.size() : s_maxItemsToCheck};

                        for (int i = 0; i < maxEntriesToCheck; ++i)
                        {
                            auto index {valueRow[i].m_values[indexFieldPos].toInt()};
                            maxIndex = maxIndex < index ? index : maxIndex;
                        }
                        type.m_maxIndex = maxIndex;
                        type.m_indexFieldIndex = indexFieldPos;
                    }
                }
            }
            else
            {
                QString error("No unit description for data type " + type.m_name + " found");
                errors.append(error);
            }
            // handle multiplier data
            if(m_typeIDToMultiplierFieldInfo.contains(type.m_ID))
            {
                for(const qint8 multID : m_typeIDToMultiplierFieldInfo.value(type.m_ID))
                {
                    if(m_multiplierStorage.contains(static_cast<quint8>(multID)))
                    {
                        type.m_multipliers.append(m_multiplierStorage.value(static_cast<quint8>(multID)));
                    }
                    else
                    {
                        QString error("Found multiplier ID " + QString::number(multID) + " but no matching multiplier");
                        errors.append(error);
                    }
                }
            }
            else
            {
                QString error("No multiplier description for data type " + type.m_name + " found");
                errors.append(error);
            }
        }
    }

    setTimeStamp(timeStampName, divisor);

    return errors;
}

bool LogdataStorage::ModelIsScaled() const
{
    return !m_typeIDToMultiplierFieldInfo.empty();
}

QString LogdataStorage::getLabelName(int index, const dataType & type)
{
    QString label = type.m_labels.at(index);
    if(index < type.m_units.size())         // do we have a unit?
    {
        const QString &unit = type.m_units.at(index);
        if(unit.size() != 0)    // only add if unit is not empty
        {
            label.append(' ');
            label.append(s_UnitParOpen);
            label.append(unit);
            label.append(s_UnitParClose);
        }
    }
    return label;
}

