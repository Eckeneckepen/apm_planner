#include "QGCMapToolBar.h"
#include "UASManager.h"
#include "ArduPilotMegaMAV.h"
#include "QGCMapWidget.h"
#include "ui_QGCMapToolBar.h"

QGCMapToolBar::QGCMapToolBar(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::QGCMapToolBar),
    map(NULL),
    optionsMenu(this),
    trailPlotMenu(this),
    updateTimesMenu(this),
    mapTypesMenu(this),
    trailSettingsGroup(new QActionGroup(this)),
    updateTimesGroup(new QActionGroup(this)),
    mapTypesGroup(new QActionGroup(this))
{
    ui->setupUi(this);
}

static const struct {
    const char*    name;
    MapType::Types type;
} sMapTypes[] = {
    { "Bing Hybrid", MapType::BingHybrid },
    { "Bing Map", MapType::BingMap },
    { "Bing Satellite", MapType::BingSatellite },
    { "Google Hybrid", MapType::GoogleHybrid },
    { "Google Map", MapType::GoogleMap },
    { "Google Satellite", MapType::GoogleSatellite },
    { "Google Terrain", MapType::GoogleTerrain },
    { "OpenStreetMap", MapType::OpenStreetMap },
    { "ArcGIS Map", MapType::ArcGIS_Map },
    { "ArcGIS Terrain", MapType::ArcGIS_Terrain },
    { "ArcGIS World Topo", MapType::ArcGIS_WorldTopo },
    { "Statkart Topo", MapType::Statkart_Topo },
    { "Statkart Basemap", MapType::Statkart_Basemap },
    { "Eniro N,S,F,D,P", MapType::Eniro_Topo },
    { "Japan Map", MapType::JapanMap },
};

static const size_t sNumMapTypes = sizeof(sMapTypes) / sizeof(sMapTypes[0]);

void QGCMapToolBar::setMap(QGCMapWidget* map)
{
    this->map = map;

    loadSettings();

    if (map)
    {
        connect(ui->goToButton, SIGNAL(clicked()), map, SLOT(showGoToDialog()));
        connect(ui->goHomeButton, SIGNAL(clicked()), this, SLOT(goHome()));
        connect(ui->lastPosButton, SIGNAL(clicked()), map, SLOT(lastPosition()));
        connect(ui->clearTrailsButton, SIGNAL(clicked()), map, SLOT(deleteTrails()));
        connect(map, SIGNAL(OnTileLoadStart()), this, SLOT(tileLoadStart()));
        connect(map, SIGNAL(OnTileLoadComplete()), this, SLOT(tileLoadEnd()));
        connect(map, SIGNAL(OnTilesStillToLoad(int)), this, SLOT(tileLoadProgress(int)));
        connect(ui->ripMapButton, SIGNAL(clicked()), map, SLOT(cacheVisibleRegion()));

        map->setFollowUAVEnabled(ui->followPushButton->isChecked());
        connect(ui->followPushButton, SIGNAL(clicked(bool)), map, SLOT(setFollowUAVEnabled(bool)));

        // Edit mode handling
        ui->editButton->hide();

        const int uavTrailTimeList[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};                      // seconds
        const int uavTrailTimeCount = 10;

        const int uavTrailDistanceList[] = {1, 2, 5, 10, 20, 50, 100, 200, 500};             // meters
        const int uavTrailDistanceCount = 9;

        // Set exclusive items
        trailSettingsGroup->setExclusive(true);
        updateTimesGroup->setExclusive(true);
        mapTypesGroup->setExclusive(true);

        // Build up menu
        trailPlotMenu.setTitle(tr("&Add trail dot every.."));
        updateTimesMenu.setTitle(tr("&Limit map view update rate to.."));
        mapTypesMenu.setTitle(tr("&Map type"));


        //setup the mapTypesMenu
        QAction* action;
        MapType::Types mapType = map->GetMapType();
        for (size_t i = 0; i < sNumMapTypes; ++i) {
            action = mapTypesMenu.addAction(tr(sMapTypes[i].name), this, SLOT(setMapType()));
            action->setData(sMapTypes[i].type);
            action->setCheckable(true);
            mapTypesGroup->addAction(action);
            if (mapType == sMapTypes[i].type) action->setChecked(true);
        }
        optionsMenu.addMenu(&mapTypesMenu);

        // FIXME MARK CURRENT VALUES IN MENU
        QAction *defaultTrailAction = trailPlotMenu.addAction(tr("No trail"), this, SLOT(setUAVTrailTime()));
        defaultTrailAction->setData(-1);
        defaultTrailAction->setCheckable(true);
        trailSettingsGroup->addAction(defaultTrailAction);

        for (int i = 0; i < uavTrailTimeCount; ++i)
        {
            action = trailPlotMenu.addAction(tr("%1 second%2").arg(uavTrailTimeList[i]).arg((uavTrailTimeList[i] > 1) ? "s" : ""), this, SLOT(setUAVTrailTime()));
            action->setData(uavTrailTimeList[i]);
            action->setCheckable(true);
            trailSettingsGroup->addAction(action);
            if (static_cast<mapcontrol::UAVTrailType::Types>(map->getTrailType()) == mapcontrol::UAVTrailType::ByTimeElapsed && map->getTrailInterval() == uavTrailTimeList[i])
            {
                // This is the current active time, set the action checked
                action->setChecked(true);
            }
        }
        for (int i = 0; i < uavTrailDistanceCount; ++i)
        {
            action = trailPlotMenu.addAction(tr("%1 meter%2").arg(uavTrailDistanceList[i]).arg((uavTrailDistanceList[i] > 1) ? "s" : ""), this, SLOT(setUAVTrailDistance()));
            action->setData(uavTrailDistanceList[i]);
            action->setCheckable(true);
            trailSettingsGroup->addAction(action);
            if (static_cast<mapcontrol::UAVTrailType::Types>(map->getTrailType()) == mapcontrol::UAVTrailType::ByDistance && map->getTrailInterval() == uavTrailDistanceList[i])
            {
                // This is the current active time, set the action checked
                action->setChecked(true);
            }
        }

        // Set no trail checked if no action is checked yet
        if (!trailSettingsGroup->checkedAction())
        {
            defaultTrailAction->setChecked(true);
        }

        optionsMenu.addMenu(&trailPlotMenu);

        // Add update times menu
        for (int i = 100; i < 5000; i+=400)
        {
            float time = i/1000.0f; // Convert from ms to seconds
            QAction* action = updateTimesMenu.addAction(tr("%1 seconds").arg(time), this, SLOT(setUpdateInterval()));
            action->setData(time);
            action->setCheckable(true);
            if (time == map->getUpdateRateLimit())
            {
                action->blockSignals(true);
                action->setChecked(true);
                action->blockSignals(false);
            }
            updateTimesGroup->addAction(action);
        }

        // If the current time is not part of the menu defaults
        // still add it as new option
        if (!updateTimesGroup->checkedAction())
        {
            float time = map->getUpdateRateLimit();
            QAction* action = updateTimesMenu.addAction(tr("uptate every %1 seconds").arg(time), this, SLOT(setUpdateInterval()));
            action->setData(time);
            action->setCheckable(true);
            action->setChecked(true);
            updateTimesGroup->addAction(action);
        }
        optionsMenu.addMenu(&updateTimesMenu);

        ui->optionsButton->setMenu(&optionsMenu);
    }
}

void QGCMapToolBar::setUAVTrailTime()
{
    QObject* sender = QObject::sender();
    QAction* action = qobject_cast<QAction*>(sender);

    if (action)
    {
        bool ok;
        int trailTime = action->data().toInt(&ok);
        if (ok)
        {
            (map->setTrailModeTimed(trailTime));
            ui->posLabel->setText(tr("Trail mode: Every %1 second%2").arg(trailTime).arg((trailTime > 1) ? "s" : ""));
        }
    }
}

void QGCMapToolBar::setUAVTrailDistance()
{
    QObject* sender = QObject::sender();
    QAction* action = qobject_cast<QAction*>(sender);

    if (action)
    {
        bool ok;
        int trailDistance = action->data().toInt(&ok);
        if (ok)
        {
            map->setTrailModeDistance(trailDistance);
            ui->posLabel->setText(tr("Trail mode: Every %1 meter%2").arg(trailDistance).arg((trailDistance == 1) ? "s" : ""));
        }
    }
}

void QGCMapToolBar::setUpdateInterval()
{
    QObject* sender = QObject::sender();
    QAction* action = qobject_cast<QAction*>(sender);

    if (action)
    {
        bool ok;
        float time = action->data().toFloat(&ok);
        if (ok)
        {
            map->setUpdateRateLimit(time);
            ui->posLabel->setText(tr("Map update rate limit: %1 second%2").arg(time).arg((time != 1.0f) ? "s" : ""));
        }
    }
}

void QGCMapToolBar::setMapType()
{
    QObject* sender = QObject::sender();
    QAction* action = qobject_cast<QAction*>(sender);

    if (action)
    {
        bool ok;
        int mapType = action->data().toInt(&ok);
        if (ok)
        {
            map->SetMapType((MapType::Types)mapType);
            ui->posLabel->setText(tr("Map type: %1").arg(mapType));
        }
    }
}

void QGCMapToolBar::tileLoadStart()
{
    ui->posLabel->setText(tr("Starting to load tiles.."));
}

void QGCMapToolBar::tileLoadEnd()
{
    ui->posLabel->setText(tr("Finished"));
}

void QGCMapToolBar::tileLoadProgress(int progress)
{
    if (progress == 1)
    {
        ui->posLabel->setText(tr("1 tile to load.."));
    }
    else if (progress > 0)
    {
        ui->posLabel->setText(tr("%1 tiles to load..").arg(progress));
    }
    else
    {
        tileLoadEnd();
    }
}

void QGCMapToolBar::goHome()
{
    UASManager *umanager = UASManager::instance();
    if (umanager){
        ArduPilotMegaMAV* apmUas= dynamic_cast<ArduPilotMegaMAV*>(umanager->getActiveUAS());
        if (apmUas){
            UASWaypointManager* wpManager = apmUas->getWaypointManager();
            const Waypoint* homeWp = wpManager->getWaypoint(0); // Waypoint 0 is home in APM
            if (homeWp){
                map->updateHomePosition(homeWp->getLatitude(), homeWp->getLongitude(), homeWp->getAltitude());
                map->goHome();
            }
        } else {
            map->goHome();
        }
    }
}

void QGCMapToolBar::loadSettings()
{
    QSettings settings;
    settings.beginGroup("QGC_MAPTOOL");
    bool follow = settings.value("FOLLOW_UAV", false).toBool();
    ui->followPushButton->setChecked(follow);
}

void QGCMapToolBar::storeSettings()
{
    QSettings settings;
    settings.beginGroup("QGC_MAPTOOL");
    settings.setValue("FOLLOW_UAV", ui->followPushButton->isChecked());
    settings.endGroup();
    settings.sync();
}

QGCMapToolBar::~QGCMapToolBar()
{
    storeSettings();
    delete ui;
    delete trailSettingsGroup;
    delete updateTimesGroup;
    delete mapTypesGroup;
    // FIXME Delete all actions
}
