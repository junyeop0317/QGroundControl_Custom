/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "PlanMasterController.h"
#include "QGCApplication.h"
#include "QGCCorePlugin.h"
#include "MultiVehicleManager.h"
#include "Vehicle.h"
#include "SettingsManager.h"
#include "AppSettings.h"
#include "JsonHelper.h"
#include "MissionManager.h"
#include "KMLPlanDomDocument.h"
#include "SurveyPlanCreator.h"
#include "StructureScanPlanCreator.h"
#include "CorridorScanPlanCreator.h"
#include "BlankPlanCreator.h"
#include "QmlObjectListModel.h"
#include "GeoFenceManager.h"
#include "RallyPointManager.h"
#include "QGCLoggingCategory.h"
#include "QGCMapPolygon.h"
#include "SurveyComplexItem.h"
#include <QProcessEnvironment>
#include <algorithm>
#include <QUrl>
#include <QDebug>
#include <QtCore/QJsonDocument>
#include <QtCore/QFileInfo>
#include <QDir>
#include <QVariant>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QGeoCoordinate>
#include <QJsonArray>


QGC_LOGGING_CATEGORY(PlanControllerLog, "PlanControllerLog")
QGC_LOGGING_CATEGORY(PlanMasterControllerLog, "PlanMasterControllerLog")

PlanMasterController::PlanMasterController(QObject* parent)
    : QObject               (parent)
      , _multiVehicleMgr      (MultiVehicleManager::instance())
      , _controllerVehicle    (new Vehicle(Vehicle::MAV_AUTOPILOT_TRACK, Vehicle::MAV_TYPE_TRACK, this))
      , _managerVehicle       (_controllerVehicle)
      , _missionController    (this)
      , _geoFenceController   (this)
      , _rallyPointController (this)
{
    _commonInit();
}

#ifdef QT_DEBUG
PlanMasterController::PlanMasterController(MAV_AUTOPILOT firmwareType, MAV_TYPE vehicleType, QObject* parent)
    : QObject               (parent)
      , _multiVehicleMgr      (MultiVehicleManager::instance())
      , _controllerVehicle    (new Vehicle(firmwareType, vehicleType))
      , _managerVehicle       (_controllerVehicle)
      , _missionController    (this)
      , _geoFenceController   (this)
      , _rallyPointController (this)
{
    _commonInit();
}
#endif

void PlanMasterController::_commonInit(void)
{
    _previousOverallDirty = dirty();
    connect(&_missionController,    &MissionController::dirtyChanged,               this, &PlanMasterController::_updateOverallDirty);
    connect(&_geoFenceController,   &GeoFenceController::dirtyChanged,              this, &PlanMasterController::_updateOverallDirty);
    connect(&_rallyPointController, &RallyPointController::dirtyChanged,            this, &PlanMasterController::_updateOverallDirty);

    connect(&_missionController,    &MissionController::containsItemsChanged,       this, &PlanMasterController::containsItemsChanged);
    connect(&_geoFenceController,   &GeoFenceController::containsItemsChanged,      this, &PlanMasterController::containsItemsChanged);
    connect(&_rallyPointController, &RallyPointController::containsItemsChanged,    this, &PlanMasterController::containsItemsChanged);

    connect(&_missionController,    &MissionController::syncInProgressChanged,      this, &PlanMasterController::syncInProgressChanged);
    connect(&_geoFenceController,   &GeoFenceController::syncInProgressChanged,     this, &PlanMasterController::syncInProgressChanged);
    connect(&_rallyPointController, &RallyPointController::syncInProgressChanged,   this, &PlanMasterController::syncInProgressChanged);

            // Offline vehicle can change firmware/vehicle type
    connect(_controllerVehicle,     &Vehicle::vehicleTypeChanged,                   this, &PlanMasterController::_updatePlanCreatorsList);
}


PlanMasterController::~PlanMasterController()
{

}

void PlanMasterController::start(void)
{
    _missionController.start    (_flyView);
    _geoFenceController.start   (_flyView);
    _rallyPointController.start (_flyView);

    _activeVehicleChanged(_multiVehicleMgr->activeVehicle());
    connect(_multiVehicleMgr, &MultiVehicleManager::activeVehicleChanged, this, &PlanMasterController::_activeVehicleChanged);

    _updatePlanCreatorsList();
}

void PlanMasterController::startStaticActiveVehicle(Vehicle* vehicle, bool deleteWhenSendCompleted)
{
    _flyView = true;
    _deleteWhenSendCompleted = deleteWhenSendCompleted;
    _missionController.start(_flyView);
    _geoFenceController.start(_flyView);
    _rallyPointController.start(_flyView);
    _activeVehicleChanged(vehicle);
}

void PlanMasterController::_activeVehicleChanged(Vehicle* activeVehicle)
{
    if (_managerVehicle == activeVehicle) {
        // We are already setup for this vehicle
        return;
    }

    qCDebug(PlanMasterControllerLog) << "_activeVehicleChanged" << activeVehicle;

    if (_managerVehicle) {
        // Disconnect old vehicle. Be careful of wildcarding disconnect too much since _managerVehicle may equal _controllerVehicle
        disconnect(_managerVehicle->missionManager(),       nullptr, this, nullptr);
        disconnect(_managerVehicle->geoFenceManager(),      nullptr, this, nullptr);
        disconnect(_managerVehicle->rallyPointManager(),    nullptr, this, nullptr);
    }

    bool newOffline = false;
    if (activeVehicle == nullptr) {
        // Since there is no longer an active vehicle we use the offline controller vehicle as the manager vehicle
        _managerVehicle = _controllerVehicle;
        newOffline = true;
    } else {
        newOffline = false;
        _managerVehicle = activeVehicle;

                // Update controllerVehicle to the currently connected vehicle
        AppSettings* appSettings = SettingsManager::instance()->appSettings();
        appSettings->offlineEditingFirmwareClass()->setRawValue(QGCMAVLink::firmwareClass(_managerVehicle->firmwareType()));
        appSettings->offlineEditingVehicleClass()->setRawValue(QGCMAVLink::vehicleClass(_managerVehicle->vehicleType()));

                // We use these signals to sequence upload and download to the multiple controller/managers
        connect(_managerVehicle->missionManager(),      &MissionManager::newMissionItemsAvailable,  this, &PlanMasterController::_loadMissionComplete);
        connect(_managerVehicle->geoFenceManager(),     &GeoFenceManager::loadComplete,             this, &PlanMasterController::_loadGeoFenceComplete);
        connect(_managerVehicle->rallyPointManager(),   &RallyPointManager::loadComplete,           this, &PlanMasterController::_loadRallyPointsComplete);
        connect(_managerVehicle->missionManager(),      &MissionManager::sendComplete,              this, &PlanMasterController::_sendMissionComplete);
        connect(_managerVehicle->geoFenceManager(),     &GeoFenceManager::sendComplete,             this, &PlanMasterController::_sendGeoFenceComplete);
        connect(_managerVehicle->rallyPointManager(),   &RallyPointManager::sendComplete,           this, &PlanMasterController::_sendRallyPointsComplete);
    }

    _offline = newOffline;
    emit offlineChanged(offline());
    emit managerVehicleChanged(_managerVehicle);

    if (_flyView) {
        // We are in the Fly View
        if (newOffline) {
            // No active vehicle, clear mission
            qCDebug(PlanMasterControllerLog) << "_activeVehicleChanged: Fly View - No active vehicle, clearing stale plan";
            removeAll();
        } else {
            // Fly view has changed to a new active vehicle, update to show correct mission
            qCDebug(PlanMasterControllerLog) << "_activeVehicleChanged: Fly View - New active vehicle, loading new plan from manager vehicle";
            _showPlanFromManagerVehicle();
        }
    } else {
        // We are in the Plan view.
        if (containsItems()) {
            // The plan view has a stale plan in it
            if (dirty()) {
                // Plan is dirty, the user must decide what to do in all cases
                qCDebug(PlanMasterControllerLog) << "_activeVehicleChanged: Plan View - Previous dirty plan exists, no new active vehicle, sending promptForPlanUsageOnVehicleChange signal";
                emit promptForPlanUsageOnVehicleChange();
            } else {
                // Plan is not dirty
                if (newOffline) {
                    // The active vehicle went away with no new active vehicle
                    qCDebug(PlanMasterControllerLog) << "_activeVehicleChanged: Plan View - Previous clean plan exists, no new active vehicle, clear stale plan";
                    removeAll();
                } else {
                    // We are transitioning from one active vehicle to another. Show the plan from the new vehicle.
                    qCDebug(PlanMasterControllerLog) << "_activeVehicleChanged: Plan View - Previous clean plan exists, new active vehicle, loading from new manager vehicle";
                    _showPlanFromManagerVehicle();
                }
            }
        } else {
            // There is no previous Plan in the view
            if (newOffline) {
                // Nothing special to do in this case
                qCDebug(PlanMasterControllerLog) << "_activeVehicleChanged: Plan View - No previous plan, no longer connected to vehicle, nothing to do";
            } else {
                // Just show the plan from the new vehicle
                qCDebug(PlanMasterControllerLog) << "_activeVehicleChanged: Plan View - No previous plan, new active vehicle, loading from new manager vehicle";
                _showPlanFromManagerVehicle();
            }
        }
    }

            // Vehicle changed so we need to signal everything
    emit containsItemsChanged(containsItems());
    emit syncInProgressChanged();
    emit dirtyChanged(dirty());

    _updatePlanCreatorsList();
}

void PlanMasterController::loadFromVehicle(void)
{
    SharedLinkInterfacePtr sharedLink = _managerVehicle->vehicleLinkManager()->primaryLink().lock();
    if (sharedLink) {
        if (sharedLink->linkConfiguration()->isHighLatency()) {
            qgcApp()->showAppMessage(tr("Download not supported on high latency links."));
            return;
        }
    } else {
        // Vehicle is shutting down
        return;
    }

    if (offline()) {
        qCWarning(PlanMasterControllerLog) << "PlanMasterController::loadFromVehicle called while offline";
    } else if (_flyView) {
        qCWarning(PlanMasterControllerLog) << "PlanMasterController::loadFromVehicle called from Fly view";
    } else if (syncInProgress()) {
        qCWarning(PlanMasterControllerLog) << "PlanMasterController::loadFromVehicle called while syncInProgress";
    } else {
        _loadGeoFence = true;
        qCDebug(PlanMasterControllerLog) << "PlanMasterController::loadFromVehicle calling _missionController.loadFromVehicle";
        _missionController.loadFromVehicle();
        setDirty(false);
    }
}


void PlanMasterController::_loadMissionComplete(void)
{
    if (!_flyView && _loadGeoFence) {
        _loadGeoFence = false;
        _loadRallyPoints = true;
        if (_geoFenceController.supported()) {
            qCDebug(PlanMasterControllerLog) << "PlanMasterController::_loadMissionComplete calling _geoFenceController.loadFromVehicle";
            _geoFenceController.loadFromVehicle();
        } else {
            qCDebug(PlanMasterControllerLog) << "PlanMasterController::_loadMissionComplete GeoFence not supported skipping";
            _geoFenceController.removeAll();
            _loadGeoFenceComplete();
        }
        setDirty(false);
    }
}

void PlanMasterController::_loadGeoFenceComplete(void)
{
    if (!_flyView && _loadRallyPoints) {
        _loadRallyPoints = false;
        if (_rallyPointController.supported()) {
            qCDebug(PlanMasterControllerLog) << "PlanMasterController::_loadGeoFenceComplete calling _rallyPointController.loadFromVehicle";
            _rallyPointController.loadFromVehicle();
        } else {
            qCDebug(PlanMasterControllerLog) << "PlanMasterController::_loadMissionComplete Rally Points not supported skipping";
            _rallyPointController.removeAll();
            _loadRallyPointsComplete();
        }
        setDirty(false);
    }
}

void PlanMasterController::_loadRallyPointsComplete(void)
{
    qCDebug(PlanMasterControllerLog) << "PlanMasterController::_loadRallyPointsComplete";
}

void PlanMasterController::_sendMissionComplete(void)
{
    if (_sendGeoFence) {
        _sendGeoFence = false;
        _sendRallyPoints = true;
        if (_geoFenceController.supported()) {
            qCDebug(PlanMasterControllerLog) << "PlanMasterController::sendToVehicle start GeoFence sendToVehicle";
            _geoFenceController.sendToVehicle();
        } else {
            qCDebug(PlanMasterControllerLog) << "PlanMasterController::sendToVehicle GeoFence not supported skipping";
            _sendGeoFenceComplete();
        }
        setDirty(false);
    }
}

void PlanMasterController::_sendGeoFenceComplete(void)
{
    if (_sendRallyPoints) {
        _sendRallyPoints = false;
        if (_rallyPointController.supported()) {
            qCDebug(PlanMasterControllerLog) << "PlanMasterController::sendToVehicle start rally sendToVehicle";
            _rallyPointController.sendToVehicle();
        } else {
            qCDebug(PlanMasterControllerLog) << "PlanMasterController::sendToVehicle Rally Points not support skipping";
            _sendRallyPointsComplete();
        }
    }
}

void PlanMasterController::_sendRallyPointsComplete(void)
{
    qCDebug(PlanMasterControllerLog) << "PlanMasterController::sendToVehicle Rally Point send complete";
    if (_deleteWhenSendCompleted) {
        this->deleteLater();
    }
}

void PlanMasterController::sendToVehicle(void)
{
    SharedLinkInterfacePtr sharedLink = _managerVehicle->vehicleLinkManager()->primaryLink().lock();
    if (sharedLink) {
        if (sharedLink->linkConfiguration()->isHighLatency()) {
            qgcApp()->showAppMessage(tr("Upload not supported on high latency links."));
            return;
        }
    } else {
        // Vehicle is shutting down
        return;
    }

    if (offline()) {
        qCWarning(PlanMasterControllerLog) << "PlanMasterController::sendToVehicle called while offline";
    } else if (syncInProgress()) {
        qCWarning(PlanMasterControllerLog) << "PlanMasterController::sendToVehicle called while syncInProgress";
    } else {
        qCDebug(PlanMasterControllerLog) << "PlanMasterController::sendToVehicle start mission sendToVehicle";
        _sendGeoFence = true;
        _missionController.sendToVehicle();
        setDirty(false);
    }
}

void PlanMasterController::loadFromFile(const QString& filename)
{
    QString errorString;
    QString errorMessage = tr("Error loading Plan file (%1). %2").arg(filename).arg("%1");

    if (filename.isEmpty()) {
        return;
    }

    QFileInfo fileInfo(filename);
    QFile file(filename);

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        errorString = file.errorString() + QStringLiteral(" ") + filename;
        qgcApp()->showAppMessage(errorMessage.arg(errorString));
        return;
    }

    bool success = false;
    if (fileInfo.suffix() == AppSettings::missionFileExtension) {
        if (!_missionController.loadJsonFile(file, errorString)) {
            qgcApp()->showAppMessage(errorMessage.arg(errorString));
        } else {
            success = true;
        }
    } else if (fileInfo.suffix() == AppSettings::waypointsFileExtension || fileInfo.suffix() == QStringLiteral("txt")) {
        if (!_missionController.loadTextFile(file, errorString)) {
            qgcApp()->showAppMessage(errorMessage.arg(errorString));
        } else {
            success = true;
        }
    } else {
        QJsonDocument   jsonDoc;
        QByteArray      bytes = file.readAll();

        if (!JsonHelper::isJsonFile(bytes, jsonDoc, errorString)) {
            qgcApp()->showAppMessage(errorMessage.arg(errorString));
            return;
        }

        QJsonObject json = jsonDoc.object();
        //-- Allow plugins to pre process the load
        QGCCorePlugin::instance()->preLoadFromJson(this, json);

        int version;
        if (!JsonHelper::validateExternalQGCJsonFile(json, kPlanFileType, kPlanFileVersion, kPlanFileVersion, version, errorString)) {
            qgcApp()->showAppMessage(errorMessage.arg(errorString));
            return;
        }

        QList<JsonHelper::KeyValidateInfo> rgKeyInfo = {
                                                        { kJsonMissionObjectKey,        QJsonValue::Object, true },
                                                        { kJsonGeoFenceObjectKey,       QJsonValue::Object, true },
                                                        { kJsonRallyPointsObjectKey,    QJsonValue::Object, true },
                                                        };
        if (!JsonHelper::validateKeys(json, rgKeyInfo, errorString)) {
            qgcApp()->showAppMessage(errorMessage.arg(errorString));
            return;
        }

        if (!_missionController.load(json[kJsonMissionObjectKey].toObject(), errorString) ||
            !_geoFenceController.load(json[kJsonGeoFenceObjectKey].toObject(), errorString) ||
            !_rallyPointController.load(json[kJsonRallyPointsObjectKey].toObject(), errorString)) {
            qgcApp()->showAppMessage(errorMessage.arg(errorString));
        } else {
            //-- Allow plugins to post process the load
            QGCCorePlugin::instance()->postLoadFromJson(this, json);
            success = true;
        }
    }

    if(success){
        _currentPlanFile = QString::asprintf("%s/%s.%s", fileInfo.path().toLocal8Bit().data(), fileInfo.completeBaseName().toLocal8Bit().data(), AppSettings::planFileExtension);
    } else {
        _currentPlanFile.clear();
    }
    emit currentPlanFileChanged();

    if (!offline()) {
        setDirty(true);
    }
}

QJsonDocument PlanMasterController::saveToJson()
{
    QJsonObject planJson;
    QGCCorePlugin::instance()->preSaveToJson(this, planJson);
    QJsonObject missionJson;
    QJsonObject fenceJson;
    QJsonObject rallyJson;
    JsonHelper::saveQGCJsonFileHeader(planJson, kPlanFileType, kPlanFileVersion);
    //-- Allow plugin to preemptly add its own keys to mission
    QGCCorePlugin::instance()->preSaveToMissionJson(this, missionJson);
    _missionController.save(missionJson);
    //-- Allow plugin to add its own keys to mission
    QGCCorePlugin::instance()->postSaveToMissionJson(this, missionJson);
    _geoFenceController.save(fenceJson);
    _rallyPointController.save(rallyJson);
    planJson[kJsonMissionObjectKey] = missionJson;
    planJson[kJsonGeoFenceObjectKey] = fenceJson;
    planJson[kJsonRallyPointsObjectKey] = rallyJson;
    QGCCorePlugin::instance()->postSaveToJson(this, planJson);
    return QJsonDocument(planJson);
}

void
PlanMasterController::saveToCurrent()
{
    if(!_currentPlanFile.isEmpty()) {
        saveToFile(_currentPlanFile);
    }
}

void PlanMasterController::saveToFile(const QString& filename)
{
    if (filename.isEmpty()) {
        return;
    }

    QString planFilename = filename;
    if (!QFileInfo(filename).fileName().contains(".")) {
        planFilename += QString(".%1").arg(fileExtension());
    }

    QFile file(planFilename);

    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qgcApp()->showAppMessage(tr("Plan save error %1 : %2").arg(filename).arg(file.errorString()));
        _currentPlanFile.clear();
        emit currentPlanFileChanged();
    } else {
        QJsonDocument saveDoc = saveToJson();
        file.write(saveDoc.toJson());
        if(_currentPlanFile != planFilename) {
            _currentPlanFile = planFilename;
            emit currentPlanFileChanged();
        }
    }

            // Only clear dirty bit if we are offline
    if (offline()) {
        setDirty(false);
    }
}

void PlanMasterController::saveToKml(const QString& filename)
{
    if (filename.isEmpty()) {
        return;
    }

    QString kmlFilename = filename;
    if (!QFileInfo(filename).fileName().contains(".")) {
        kmlFilename += QString(".%1").arg(kmlFileExtension());
    }

    QFile file(kmlFilename);

    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qgcApp()->showAppMessage(tr("KML save error %1 : %2").arg(filename).arg(file.errorString()));
    } else {
        KMLPlanDomDocument planKML;
        _missionController.addMissionToKML(planKML);
        QTextStream stream(&file);
        stream << planKML.toString();
        file.close();
    }
}

void PlanMasterController::removeAll(void)
{
    _missionController.removeAll();
    _geoFenceController.removeAll();
    _rallyPointController.removeAll();
    if (_offline) {
        _missionController.setDirty(false);
        _geoFenceController.setDirty(false);
        _rallyPointController.setDirty(false);
        _currentPlanFile.clear();
        emit currentPlanFileChanged();
    }
}

void PlanMasterController::removeAllFromVehicle(void)
{
    if (!offline()) {
        _missionController.removeAllFromVehicle();
        if (_geoFenceController.supported()) {
            _geoFenceController.removeAllFromVehicle();
        }
        if (_rallyPointController.supported()) {
            _rallyPointController.removeAllFromVehicle();
        }
        setDirty(false);
    } else {
        qWarning() << "PlanMasterController::removeAllFromVehicle called while offline";
    }
}

bool PlanMasterController::containsItems(void) const
{
    return _missionController.containsItems() || _geoFenceController.containsItems() || _rallyPointController.containsItems();
}

bool PlanMasterController::dirty(void) const
{
    return _missionController.dirty() || _geoFenceController.dirty() || _rallyPointController.dirty();
}

void PlanMasterController::setDirty(bool dirty)
{
    _missionController.setDirty(dirty);
    _geoFenceController.setDirty(dirty);
    _rallyPointController.setDirty(dirty);
}

QString PlanMasterController::fileExtension(void) const
{
    return AppSettings::planFileExtension;
}

QString PlanMasterController::kmlFileExtension(void) const
{
    return AppSettings::kmlFileExtension;
}

QStringList PlanMasterController::loadNameFilters(void) const
{
    QStringList filters;

    filters << tr("Supported types (*.%1 *.%2 *.%3 *.%4)").arg(AppSettings::planFileExtension).arg(AppSettings::missionFileExtension).arg(AppSettings::waypointsFileExtension).arg("txt") <<
        tr("All Files (*)");
    return filters;
}


QStringList PlanMasterController::saveNameFilters(void) const
{
    QStringList filters;

    filters << tr("Plan Files (*.%1)").arg(fileExtension()) << tr("All Files (*)");
    return filters;
}

void PlanMasterController::sendPlanToVehicle(Vehicle* vehicle, const QString& filename)
{
    // Use a transient PlanMasterController to accomplish this
    PlanMasterController* controller = new PlanMasterController();
    controller->startStaticActiveVehicle(vehicle, true /* deleteWhenSendCompleted */);
    controller->loadFromFile(filename);
    controller->sendToVehicle();
}

void PlanMasterController::_showPlanFromManagerVehicle(void)
{
    if (!_managerVehicle->initialPlanRequestComplete() && !syncInProgress()) {
        // Something went wrong with initial load. All controllers are idle, so just force it off
        _managerVehicle->forceInitialPlanRequestComplete();
    }

            // The crazy if structure is to handle the load propagating by itself through the system
    if (!_missionController.showPlanFromManagerVehicle()) {
        if (!_geoFenceController.showPlanFromManagerVehicle()) {
            _rallyPointController.showPlanFromManagerVehicle();
        }
    }
}

bool PlanMasterController::syncInProgress(void) const
{
    return _missionController.syncInProgress() ||
           _geoFenceController.syncInProgress() ||
           _rallyPointController.syncInProgress();
}

bool PlanMasterController::isEmpty(void) const
{
    return _missionController.isEmpty() &&
           _geoFenceController.isEmpty() &&
           _rallyPointController.isEmpty();
}

void PlanMasterController::_updateOverallDirty(void)
{
    if(_previousOverallDirty != dirty()){
        _previousOverallDirty = dirty();
        emit dirtyChanged(_previousOverallDirty);
    }
}

void PlanMasterController::_updatePlanCreatorsList(void)
{
    if (!_flyView) {
        if (!_planCreators) {
            _planCreators = new QmlObjectListModel(this);
            _planCreators->append(new BlankPlanCreator(this, this));
            _planCreators->append(new SurveyPlanCreator(this, this));
            _planCreators->append(new CorridorScanPlanCreator(this, this));
            emit planCreatorsChanged(_planCreators);
        }

        if (_managerVehicle->fixedWing()) {
            if (_planCreators->count() == 4) {
                _planCreators->removeAt(_planCreators->count() - 1);
            }
        } else {
            if (_planCreators->count() != 4) {
                _planCreators->append(new StructureScanPlanCreator(this, this));
            }
        }
    }
}

void PlanMasterController::showPlanFromManagerVehicle(void)
{
    if (offline()) {
        // There is no new vehicle so clear any previous plan
        qCDebug(PlanMasterControllerLog) << "showPlanFromManagerVehicle: Plan View - No new vehicle, clear any previous plan";
        removeAll();
    } else {
        // We have a new active vehicle, show the plan from that
        qCDebug(PlanMasterControllerLog) << "showPlanFromManagerVehicle: Plan View - New vehicle available, show plan from new manager vehicle";
        _showPlanFromManagerVehicle();
    }
}


// 폴리곤 중심 계산 헬퍼 함수
QGeoCoordinate PlanMasterController::calculatePolygonCenter(const QList<QGeoCoordinate>& polygonPoints) {
    double latSum = 0.0, lonSum = 0.0;
    int count = polygonPoints.size();
    if (count < 3) {
        qCWarning(PlanMasterControllerLog) << "calculatePolygonCenter: Insufficient polygon points:" << count;
        return QGeoCoordinate();
    }
    for (const QGeoCoordinate& coord : polygonPoints) {
        latSum += coord.latitude();
        lonSum += coord.longitude();
    }
    QGeoCoordinate center(latSum / count, lonSum / count);
    qCDebug(PlanMasterControllerLog) << "Calculated polygon center: lat=" << center.latitude() << "lon=" << center.longitude();
    return center;
}



// 주소 검색 및 지도 이동
void PlanMasterController::searchAndGo(const QString& address, bool panAfterSearch) {
    // 주소 입력 검증
    if (address.trimmed().isEmpty() || address.length() < 2) {
        qCWarning(PlanMasterControllerLog) << "Invalid address input: Empty or too short";
        emit errorMessage(tr("잘못된 주소: 주소를 입력하세요 (최소 2자 이상)."));
        emit suggestionsReady({});
        return;
    }

    QByteArray apiKey = qgetenv("v_world_key");
    if (apiKey.isEmpty()) {
        qCWarning(PlanMasterControllerLog) << "v_world_key is not set";
        emit errorMessage(tr("V-World API 키가 설정되지 않았습니다."));
        emit suggestionsReady({});
        return;
    }

    QString requestUrl = QString("https://api.vworld.kr/req/search?service=search&request=search&version=2.0&query=%1&type=place&format=json&errorformat=json&key=%2")
                            .arg(QUrl::toPercentEncoding(address))
                            .arg(QString(apiKey));
    qCDebug(PlanMasterControllerLog) << "V-World 요청 URL:" << requestUrl;

    QNetworkAccessManager* manager = new QNetworkAccessManager(this);
    QNetworkRequest request{QUrl(requestUrl)};
    QNetworkReply* reply = manager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, panAfterSearch, manager]() {
        QVariantList suggestions;
        QSet<QString> addedTitles;
        if (reply->error() != QNetworkReply::NoError) {
            qCWarning(PlanMasterControllerLog) << "Search API error:" << reply->errorString();
            emit errorMessage(tr("네트워크 오류: %1").arg(reply->errorString()));
            emit suggestionsReady(suggestions);
            reply->deleteLater();
            manager->deleteLater();
            return;
        }

        QByteArray responseData = reply->readAll();
        qCDebug(PlanMasterControllerLog) << "Search API response:" << QString(responseData);
        QJsonDocument doc = QJsonDocument::fromJson(responseData);
        if (doc.isNull() || !doc.isObject()) {
            qCWarning(PlanMasterControllerLog) << "Invalid JSON response from search API";
            emit errorMessage(tr("잘못된 주소: 검색 API 응답을 파싱할 수 없습니다."));
            emit suggestionsReady(suggestions);
            reply->deleteLater();
            manager->deleteLater();
            return;
        }

        QJsonObject response = doc.object().value("response").toObject();
        if (response.value("status").toString() != "OK") {
            qCWarning(PlanMasterControllerLog) << "Search API status not OK:" << response.value("status").toString();
            emit errorMessage(tr("잘못된 주소: 검색 결과가 없습니다."));
            emit suggestionsReady(suggestions);
            reply->deleteLater();
            manager->deleteLater();
            return;
        }

        if (response.value("result").toObject().contains("items")) {
            QJsonArray items = response.value("result").toObject().value("items").toArray();
            if (items.isEmpty()) {
                qCWarning(PlanMasterControllerLog) << "No items found in search API response";
                emit errorMessage(tr("잘못된 주소: 검색 결과가 없습니다."));
                emit suggestionsReady(suggestions);
                reply->deleteLater();
                manager->deleteLater();
                return;
            }

            if (panAfterSearch) {
                QJsonObject firstItem = items[0].toObject();
                QJsonObject point = firstItem.value("point").toObject();
                double lon = point.value("x").toString().toDouble();
                double lat = point.value("y").toString().toDouble();
                if (!qIsNaN(lon) && !qIsNaN(lat)) {
                    addWaypointAndZoom(lat, lon);
                } else {
                    qCWarning(PlanMasterControllerLog) << "Invalid coordinates in search API response";
                    emit errorMessage(tr("잘못된 주소: 유효하지 않은 좌표가 반환되었습니다."));
                }
            }

            for (const QJsonValue &value : items) {
                QJsonObject item = value.toObject();
                QString title = item.value("title").toString();
                if (!addedTitles.contains(title)) {
                    QJsonObject point = item.value("point").toObject();
                    QVariantMap suggestionItem;
                    suggestionItem["title"] = title;
                    suggestionItem["longitude"] = point.value("x").toString().toDouble();
                    suggestionItem["latitude"] = point.value("y").toString().toDouble();
                    suggestions.append(suggestionItem);
                    addedTitles.insert(title);
                }
            }
        } else {
            qCWarning(PlanMasterControllerLog) << "No items found in search API response";
            emit errorMessage(tr("잘못된 주소: 검색 결과가 없습니다."));
        }

        qCDebug(PlanMasterControllerLog) << "QML로 추천 목록 신호 보냄. 개수:" << suggestions.count();
        emit suggestionsReady(suggestions);
        reply->deleteLater();
        manager->deleteLater();
    });
}

// 좌표 검증
void PlanMasterController::addWaypointAndZoom(double latitude, double longitude) {
    if (qIsNaN(latitude) || qIsNaN(longitude)) {
        qCWarning(PlanMasterControllerLog) << "Invalid coordinates for waypoint: lat=" << latitude << "lon=" << longitude;
        emit errorMessage(tr("잘못된 주소: 유효하지 않은 좌표입니다."));
        return;
    }

    QGeoCoordinate coordinate(latitude, longitude);
    if (!coordinate.isValid()) {
        qCWarning(PlanMasterControllerLog) << "Invalid QGeoCoordinate: lat=" << latitude << "lon=" << longitude;
        emit errorMessage(tr("잘못된 주소: 유효하지 않은 좌표입니다."));
        return;
    }

    int nextIndex = _missionController.currentPlanViewVIIndex() + 1;
    _missionController.insertSimpleMissionItem(coordinate, nextIndex, true);

    qCDebug(PlanMasterControllerLog) << "Emitting panAndZoomMap signal to QML with lat:" << coordinate.latitude() << "lon:" << coordinate.longitude() << "zoom:" << 15;
    emit panAndZoomMap(coordinate.latitude(), coordinate.longitude(), 15);
}

//도로명 주소 기반 지적도 검색 & Survey 생성
void PlanMasterController::findCadastralAndCreateSurvey(const QString& address) {
    // 주소 입력 검증
    if (address.trimmed().isEmpty()) {
        qCWarning(PlanMasterControllerLog) << "Invalid address input: Empty";
        emit errorMessage(tr("잘못된 주소: 주소를 입력하세요."));
        return;
    }

    QByteArray apiKey = qgetenv("v_world_key");
    if (apiKey.isEmpty()) {
        qCWarning(PlanMasterControllerLog) << "v_world_key is not set";
        emit errorMessage(tr("V-World API 키가 설정되지 않았습니다."));
        return;
    }

    qCDebug(PlanMasterControllerLog) << "Starting cadastral search for address:" << address;

    // 1단계: VWorld 주소 검색 API - 주소를 좌표로 변환
    QString searchUrl = QString("https://api.vworld.kr/req/address?service=address&request=getcoord&version=2.0&crs=epsg:4326&address=%1&format=json&type=road&key=%2")
                            .arg(QUrl::toPercentEncoding(address))
                            .arg(QString(apiKey));
    qCDebug(PlanMasterControllerLog) << "Step 1 - Search URL:" << searchUrl;

    QNetworkAccessManager* searchManager = new QNetworkAccessManager(this);
    connect(searchManager, &QNetworkAccessManager::finished, searchManager, &QObject::deleteLater);
    QNetworkReply* searchReply = searchManager->get(QNetworkRequest(QUrl(searchUrl)));
    connect(searchReply, &QNetworkReply::finished, this, [this, searchReply, apiKey, address]() {
        searchReply->deleteLater();

        if (searchReply->error() != QNetworkReply::NoError) {
            qCWarning(PlanMasterControllerLog) << "Step 1 Network error:" << searchReply->errorString();
            emit errorMessage(tr("네트워크 오류: %1").arg(searchReply->errorString()));
            return;
        }

        QByteArray responseData = searchReply->readAll();
        qCDebug(PlanMasterControllerLog) << "Step 1 Response:" << QString(responseData);

        QJsonDocument doc = QJsonDocument::fromJson(responseData);
        if (doc.isNull() || !doc.isObject()) {
            qCWarning(PlanMasterControllerLog) << "Invalid JSON response from address API";
            emit errorMessage(tr("잘못된 주소: API 응답을 파싱할 수 없습니다."));
            return;
        }

        QJsonObject response = doc.object().value("response").toObject();
        if (response.value("status").toString() != "OK") {
            qCWarning(PlanMasterControllerLog) << "Step 1 API Error - Status:" << response.value("status").toString();
            emit errorMessage(tr("잘못된 주소: 주소를 찾을 수 없습니다."));
            return;
        }

        QJsonObject result = response.value("result").toObject();
        if (!result.contains("point")) {
            qCWarning(PlanMasterControllerLog) << "Step 1 - No point data in response";
            emit errorMessage(tr("잘못된 주소: 좌표 데이터를 찾을 수 없습니다."));
            return;
        }

        QJsonObject point = result.value("point").toObject();
        if (!point.contains("x") || !point.contains("y")) {
            qCWarning(PlanMasterControllerLog) << "Invalid coordinates in address API response";
            emit errorMessage(tr("잘못된 주소: 유효하지 않은 좌표가 반환되었습니다."));
            return;
        }

        double lon = point.value("x").toString().toDouble();
        double lat = point.value("y").toString().toDouble();
        if (qIsNaN(lon) || qIsNaN(lat)) {
            qCWarning(PlanMasterControllerLog) << "Invalid coordinate values for address:" << address;
            emit errorMessage(tr("잘못된 주소: 유효하지 않은 좌표 값입니다."));
            return;
        }

        qCDebug(PlanMasterControllerLog) << "Step 1 Success - Found coordinates: Lat=" << lat << "Lon=" << lon;

        // 2단계: VWorld 지적도 API - 해당 좌표의 토지 경계 폴리곤 검색
        QString cadastralUrl = QString("https://api.vworld.kr/req/data?service=data&version=2.0&request=GetFeature&format=json&size=1000&page=1&geometry=true&attribute=true&crs=EPSG:4326&geomFilter=POINT(%1 %2)&data=LP_PA_CBND_BUBUN&key=%3")
                                   .arg(lon).arg(lat).arg(QString(apiKey));
        qCDebug(PlanMasterControllerLog) << "Step 2 - Cadastral URL:" << cadastralUrl;

        QNetworkAccessManager* cadastralManager = new QNetworkAccessManager(this);
        connect(cadastralManager, &QNetworkAccessManager::finished, cadastralManager, &QObject::deleteLater);
        QNetworkReply* cadastralReply = cadastralManager->get(QNetworkRequest(QUrl(cadastralUrl)));
        connect(cadastralReply, &QNetworkReply::finished, this, [this, cadastralReply, address]() {
            cadastralReply->deleteLater();

            if (cadastralReply->error() != QNetworkReply::NoError) {
                qCWarning(PlanMasterControllerLog) << "Step 2 Network error:" << cadastralReply->errorString();
                emit errorMessage(tr("지적도 데이터를 가져오지 못했습니다: %1").arg(cadastralReply->errorString()));
                return;
            }

            QByteArray cadastralResponseData = cadastralReply->readAll();
            qCDebug(PlanMasterControllerLog) << "Step 2 Response:" << QString(cadastralResponseData);

            QJsonDocument cadastralDoc = QJsonDocument::fromJson(cadastralResponseData);
            if (cadastralDoc.isNull() || !cadastralDoc.isObject()) {
                qCWarning(PlanMasterControllerLog) << "Invalid JSON response from cadastral API";
                emit errorMessage(tr("잘못된 주소: 지적도 API 응답을 파싱할 수 없습니다."));
                return;
            }

            QJsonObject cadastralResponse = cadastralDoc.object().value("response").toObject();
            if (cadastralResponse.value("status").toString() != "OK") {
                qCWarning(PlanMasterControllerLog) << "Step 2 API Error - Status:" << cadastralResponse.value("status").toString();
                emit errorMessage(tr("지적도 데이터를 찾을 수 없습니다."));
                return;
            }

            QJsonArray features = cadastralResponse.value("result").toObject().value("featureCollection").toObject().value("features").toArray();
            if (features.isEmpty()) {
                qCWarning(PlanMasterControllerLog) << "No features found in cadastral data for address:" << address;
                emit errorMessage(tr("지정된 위치에서 지적도 데이터를 찾을 수 없습니다."));
                return;
            }

            QJsonObject firstFeature = features[0].toObject();
            QJsonObject properties = firstFeature.value("properties").toObject();
            QString pnu = properties.value("pnu").toString();
            QString jimok = properties.value("jimok_nm").toString();
            qCDebug(PlanMasterControllerLog) << "Step 2 Success - PNU:" << pnu << "Land Type:" << jimok;

            QJsonObject geometry = firstFeature.value("geometry").toObject();
            if (!geometry.contains("type") || !geometry.contains("coordinates")) {
                qCWarning(PlanMasterControllerLog) << "Invalid geometry in cadastral data";
                emit errorMessage(tr("잘못된 주소: 유효하지 않은 지적도 데이터입니다."));
                return;
            }

            QString geomType = geometry.value("type").toString();
            QJsonArray coordinates = geometry.value("coordinates").toArray();
            QList<QGeoCoordinate> polygonPoints;
            if (geomType == "Polygon") {
                QJsonArray polygonRing = coordinates[0].toArray();
                qCDebug(PlanMasterControllerLog) << "Processing Polygon with" << polygonRing.size() << "boundary points";
                for (const QJsonValue& pointValue : polygonRing) {
                    QJsonArray point = pointValue.toArray();
                    if (point.size() >= 2) {
                        double longitude = point[0].toDouble();
                        double latitude = point[1].toDouble();
                        QGeoCoordinate coord(latitude, longitude);
                        if (coord.isValid()) {
                            polygonPoints.append(coord);
                        }
                    }
                }
            } else if (geomType == "MultiPolygon") {
                QJsonArray firstPolygon = coordinates[0].toArray();
                QJsonArray polygonRing = firstPolygon[0].toArray();
                qCDebug(PlanMasterControllerLog) << "Processing MultiPolygon with" << polygonRing.size() << "boundary points";
                for (const QJsonValue& pointValue : polygonRing) {
                    QJsonArray point = pointValue.toArray();
                    if (point.size() >= 2) {
                        double longitude = point[0].toDouble();
                        double latitude = point[1].toDouble();
                        QGeoCoordinate coord(latitude, longitude);
                        if (coord.isValid()) {
                            polygonPoints.append(coord);
                        }
                    }
                }
            } else {
                qCWarning(PlanMasterControllerLog) << "Unsupported geometry type:" << geomType;
                emit errorMessage(tr("지원되지 않는 지적도 형상입니다: %1").arg(geomType));
                return;
            }

            if (polygonPoints.size() < 3) {
                qCWarning(PlanMasterControllerLog) << "Insufficient boundary points for survey creation:" << polygonPoints.size();
                emit errorMessage(tr("Survey 생성에 필요한 경계점이 부족합니다."));
                return;
            }

            qCDebug(PlanMasterControllerLog) << "Successfully parsed" << polygonPoints.size() << "valid boundary points";

            // Survey 미션 생성
            int nextIndex = _missionController.currentPlanViewVIIndex() + 1;
            _missionController.insertComplexMissionItem("Survey", polygonPoints.first(), nextIndex, true);

            VisualMissionItem* newSurveyItem = qobject_cast<VisualMissionItem*>(_missionController.visualItems()->get(nextIndex));
            if (newSurveyItem) {
                SurveyComplexItem* surveyItem = qobject_cast<SurveyComplexItem*>(newSurveyItem);
                if (surveyItem) {
                    QGCMapPolygon* polygon = surveyItem->surveyAreaPolygon();
                    if (polygon) {
                        polygon->setPath(polygonPoints);
                        qCDebug(PlanMasterControllerLog) << "Successfully set survey area polygon with" << polygonPoints.size() << "points";
                    } else {
                        qCWarning(PlanMasterControllerLog) << "Failed to get surveyAreaPolygon from SurveyComplexItem";
                        emit errorMessage(tr("Survey 영역 폴리곤을 설정하지 못했습니다."));
                    }
                } else {
                    qCWarning(PlanMasterControllerLog) << "Failed to cast VisualMissionItem to SurveyComplexItem";
                    emit errorMessage(tr("Survey 미션 아이템으로 변환하지 못했습니다."));
                }

                qCDebug(PlanMasterControllerLog) << "Successfully created Survey mission with" << polygonPoints.size() << "boundary points";
                emit errorMessage(tr("Survey 미션이 생성되었습니다: %1 (PNU: %2)").arg(address, pnu));

                // 폴리곤 중심으로 지도 이동
                QGeoCoordinate center = calculatePolygonCenter(polygonPoints);
                if (center.isValid()) {
                    // 동적 줌 레벨 계산
                    double maxDistance = 0.0;
                    for (int i = 0; i < polygonPoints.size(); ++i) {
                        for (int j = i + 1; j < polygonPoints.size(); ++j) {
                            double distance = polygonPoints[i].distanceTo(polygonPoints[j]);
                            maxDistance = qMax(maxDistance, distance);
                        }
                    }
                    int zoomLevel = maxDistance > 1000 ? 13 : maxDistance > 500 ? 14 : 15;
                    qCDebug(PlanMasterControllerLog) << "Emitting panAndZoomMap signal to QML with lat:" << center.latitude() << "lon:" << center.longitude() << "zoom:" << zoomLevel;
                    emit panAndZoomMap(center.latitude(), center.longitude(), zoomLevel);
                } else {
                    qCWarning(PlanMasterControllerLog) << "Invalid polygon center for address:" << address;
                    emit errorMessage(tr("폴리곤 중심을 계산할 수 없습니다."));
                }
                emit planReadyForViewing();
            } else {
                qCWarning(PlanMasterControllerLog) << "Failed to create Survey mission item";
                emit errorMessage(tr("Survey 미션 생성에 실패했습니다."));
            }
        });
    });
}

QVariantList PlanMasterController::streetResults() const {
    return _streetResults;
}


//지번(토지) 주소 검색
void PlanMasterController::searchStreet(const QString& jibunText) {
    // 입력 검증
    if (jibunText.trimmed().isEmpty()) {
        qCWarning(PlanMasterControllerLog) << "Invalid jibun address input: Empty";
        emit errorMessage(tr("잘못된 주소: 지번을 입력하세요."));
        return;
    }

    QByteArray apiKey = qgetenv("v_world_key");
    if (apiKey.isEmpty()) {
        qCWarning(PlanMasterControllerLog) << "v_world_key is not set";
        emit errorMessage(tr("V-World API 키가 설정되지 않았습니다."));
        return;
    }

    qCDebug(PlanMasterControllerLog) << "Starting cadastral search for jibun address:" << jibunText;

    // 1단계: VWorld 주소 검색 API - 지번 주소를 좌표로 변환
    QString searchUrl = QString("https://api.vworld.kr/req/address?service=address&request=getcoord&version=2.0&crs=epsg:4326&address=%1&format=json&type=parcel&key=%2")
                            .arg(QUrl::toPercentEncoding(jibunText))
                            .arg(QString(apiKey));
    qCDebug(PlanMasterControllerLog) << "Step 1 - Search URL:" << searchUrl;

    QNetworkAccessManager* searchManager = new QNetworkAccessManager(this);
    connect(searchManager, &QNetworkAccessManager::finished, searchManager, &QObject::deleteLater);
    QNetworkReply* searchReply = searchManager->get(QNetworkRequest(QUrl(searchUrl)));
    connect(searchReply, &QNetworkReply::finished, this, [this, searchReply, apiKey, jibunText]() {
        searchReply->deleteLater();

        if (searchReply->error() != QNetworkReply::NoError) {
            qCWarning(PlanMasterControllerLog) << "Step 1 Network error:" << searchReply->errorString();
            emit errorMessage(tr("네트워크 오류: %1").arg(searchReply->errorString()));
            return;
        }

        QByteArray responseData = searchReply->readAll();
        qCDebug(PlanMasterControllerLog) << "Step 1 Response:" << QString(responseData);

        QJsonDocument doc = QJsonDocument::fromJson(responseData);
        if (doc.isNull() || !doc.isObject()) {
            qCWarning(PlanMasterControllerLog) << "Invalid JSON response from address API";
            emit errorMessage(tr("잘못된 주소: API 응답을 파싱할 수 없습니다."));
            return;
        }

        QJsonObject response = doc.object().value("response").toObject();
        if (response.value("status").toString() != "OK") {
            qCWarning(PlanMasterControllerLog) << "Step 1 API Error - Status:" << response.value("status").toString();
            emit errorMessage(tr("잘못된 주소: 지번 주소를 찾을 수 없습니다."));
            return;
        }

        QJsonObject result = response.value("result").toObject();
        if (!result.contains("point")) {
            qCWarning(PlanMasterControllerLog) << "Step 1 - No point data in response";
            emit errorMessage(tr("잘못된 주소: 좌표 데이터를 찾을 수 없습니다."));
            return;
        }

        QJsonObject point = result.value("point").toObject();
        double lon = point.value("x").toString().toDouble();
        double lat = point.value("y").toString().toDouble();
        if (qIsNaN(lon) || qIsNaN(lat)) {
            qCWarning(PlanMasterControllerLog) << "Invalid coordinate values for address:" << jibunText;
            emit errorMessage(tr("잘못된 주소: 유효하지 않은 좌표 값입니다."));
            return;
        }

        qCDebug(PlanMasterControllerLog) << "Step 1 Success - Found coordinates: Lat=" << lat << "Lon=" << lon;

        // 2단계: VWorld 지적도 API - 좌표로 LP_PA_CBND_BUBUN 데이터 요청
        QString cadastralUrl = QString("https://api.vworld.kr/req/data?service=data&version=2.0&request=GetFeature&format=json&size=10&page=1&geometry=true&attribute=true&crs=EPSG:4326&geomFilter=POINT(%1 %2)&data=LP_PA_CBND_BUBUN&key=%3")
                                   .arg(lon).arg(lat).arg(QString(apiKey));
        qCDebug(PlanMasterControllerLog) << "Step 2 - Cadastral URL:" << cadastralUrl;

        QNetworkAccessManager* cadastralManager = new QNetworkAccessManager(this);
        connect(cadastralManager, &QNetworkAccessManager::finished, cadastralManager, &QObject::deleteLater);
        QNetworkReply* cadastralReply = cadastralManager->get(QNetworkRequest(QUrl(cadastralUrl)));
        connect(cadastralReply, &QNetworkReply::finished, this, [this, cadastralReply, jibunText]() {
            cadastralReply->deleteLater();

            if (cadastralReply->error() != QNetworkReply::NoError) {
                qCWarning(PlanMasterControllerLog) << "Step 2 Network error:" << cadastralReply->errorString();
                emit errorMessage(tr("지적도 데이터를 가져오지 못했습니다: %1").arg(cadastralReply->errorString()));
                return;
            }

            QByteArray cadastralResponseData = cadastralReply->readAll();
            qCDebug(PlanMasterControllerLog) << "Step 2 Response:" << QString(cadastralResponseData);

            QJsonDocument cadastralDoc = QJsonDocument::fromJson(cadastralResponseData);
            if (cadastralDoc.isNull() || !cadastralDoc.isObject()) {
                qCWarning(PlanMasterControllerLog) << "Invalid JSON response from cadastral API";
                emit errorMessage(tr("잘못된 주소: 지적도 API 응답을 파싱할 수 없습니다."));
                return;
            }

            QJsonObject cadastralResponse = cadastralDoc.object().value("response").toObject();
            if (cadastralResponse.value("status").toString() != "OK") {
                qCWarning(PlanMasterControllerLog) << "Step 2 API Error - Status:" << cadastralResponse.value("status").toString();
                emit errorMessage(tr("지적도 데이터를 찾을 수 없습니다."));
                return;
            }

            QJsonArray features = cadastralResponse.value("result").toObject().value("featureCollection").toObject().value("features").toArray();
            if (features.isEmpty()) {
                qCWarning(PlanMasterControllerLog) << "No features found in cadastral data for address:" << jibunText;
                emit errorMessage(tr("지정된 지번에서 지적도 데이터를 찾을 수 없습니다."));
                return;
            }

            QJsonObject firstFeature = features[0].toObject();
            QJsonObject properties = firstFeature.value("properties").toObject();
            QString pnu = properties.value("pnu").toString();
            QString jimok = properties.value("jimok_nm").toString();
            qCDebug(PlanMasterControllerLog) << "Step 2 Success - PNU:" << pnu << "Land Type:" << jimok;

            QJsonObject geometry = firstFeature.value("geometry").toObject();
            if (!geometry.contains("type") || !geometry.contains("coordinates")) {
                qCWarning(PlanMasterControllerLog) << "Invalid geometry in cadastral data";
                emit errorMessage(tr("잘못된 주소: 유효하지 않은 지적도 데이터입니다."));
                return;
            }

            QString geomType = geometry.value("type").toString();
            QJsonArray coordinates = geometry.value("coordinates").toArray();
            QList<QGeoCoordinate> polygonPoints;
            if (geomType == "Polygon") {
                QJsonArray polygonRing = coordinates[0].toArray();
                qCDebug(PlanMasterControllerLog) << "Processing Polygon with" << polygonRing.size() << "boundary points";
                for (const QJsonValue& pointValue : polygonRing) {
                    QJsonArray point = pointValue.toArray();
                    if (point.size() >= 2) {
                        double longitude = point[0].toDouble();
                        double latitude = point[1].toDouble();
                        QGeoCoordinate coord(latitude, longitude);
                        if (coord.isValid()) {
                            polygonPoints.append(coord);
                        }
                    }
                }
            } else if (geomType == "MultiPolygon") {
                QJsonArray firstPolygon = coordinates[0].toArray();
                QJsonArray polygonRing = firstPolygon[0].toArray();
                qCDebug(PlanMasterControllerLog) << "Processing MultiPolygon with" << polygonRing.size() << "boundary points";
                for (const QJsonValue& pointValue : polygonRing) {
                    QJsonArray point = pointValue.toArray();
                    if (point.size() >= 2) {
                        double longitude = point[0].toDouble();
                        double latitude = point[1].toDouble();
                        QGeoCoordinate coord(latitude, longitude);
                        if (coord.isValid()) {
                            polygonPoints.append(coord);
                        }
                    }
                }
            } else {
                qCWarning(PlanMasterControllerLog) << "Unsupported geometry type:" << geomType;
                emit errorMessage(tr("지원되지 않는 지적도 형상입니다: %1").arg(geomType));
                return;
            }

            if (polygonPoints.size() < 3) {
                qCWarning(PlanMasterControllerLog) << "Insufficient boundary points for survey creation:" << polygonPoints.size();
                emit errorMessage(tr("Survey 생성에 필요한 경계점이 부족합니다."));
                return;
            }

            qCDebug(PlanMasterControllerLog) << "Successfully parsed" << polygonPoints.size() << "valid boundary points";

            // Survey 미션 생성
            int nextIndex = _missionController.currentPlanViewVIIndex() + 1;
            _missionController.insertComplexMissionItem("Survey", polygonPoints.first(), nextIndex, true);

            VisualMissionItem* newSurveyItem = qobject_cast<VisualMissionItem*>(_missionController.visualItems()->get(nextIndex));
            if (newSurveyItem) {
                SurveyComplexItem* surveyItem = qobject_cast<SurveyComplexItem*>(newSurveyItem);
                if (surveyItem) {
                    QGCMapPolygon* polygon = surveyItem->surveyAreaPolygon();
                    if (polygon) {
                        polygon->setPath(polygonPoints);
                        qCDebug(PlanMasterControllerLog) << "Successfully set survey area polygon with" << polygonPoints.size() << "points";
                    } else {
                        qCWarning(PlanMasterControllerLog) << "Failed to get surveyAreaPolygon from SurveyComplexItem";
                        emit errorMessage(tr("Survey 영역 폴리곤을 설정하지 못했습니다."));
                    }
                } else {
                    qCWarning(PlanMasterControllerLog) << "Failed to cast VisualMissionItem to SurveyComplexItem";
                    emit errorMessage(tr("Survey 미션 아이템으로 변환하지 못했습니다."));
                }

                qCDebug(PlanMasterControllerLog) << "Successfully created Survey mission with" << polygonPoints.size() << "boundary points";
                emit errorMessage(tr("Survey 미션이 생성되었습니다: %1 (PNU: %2)").arg(jibunText, pnu));

                // 폴리곤 중심으로 지도 이동
                QGeoCoordinate center = calculatePolygonCenter(polygonPoints);
                if (center.isValid()) {
                    double maxDistance = 0.0;
                    for (int i = 0; i < polygonPoints.size(); ++i) {
                        for (int j = i + 1; j < polygonPoints.size(); ++j) {
                            double distance = polygonPoints[i].distanceTo(polygonPoints[j]);
                            maxDistance = qMax(maxDistance, distance);
                        }
                    }
                    int zoomLevel = maxDistance > 1000 ? 13 : maxDistance > 500 ? 14 : 15;
                    qCDebug(PlanMasterControllerLog) << "Emitting panAndZoomMap signal to QML with lat:" << center.latitude() << "lon:" << center.longitude() << "zoom:" << zoomLevel;
                    emit panAndZoomMap(center.latitude(), center.longitude(), zoomLevel);
                } else {
                    qCWarning(PlanMasterControllerLog) << "Invalid polygon center for address:" << jibunText;
                    emit errorMessage(tr("폴리곤 중심을 계산할 수 없습니다."));
                }
                emit planReadyForViewing();
            } else {
                qCWarning(PlanMasterControllerLog) << "Failed to create Survey mission item";
                emit errorMessage(tr("Survey 미션 생성에 실패했습니다."));
            }
        });
    });
}


// 지번 코드(PNU) 기반 지적도 폴리곤 로딩
void PlanMasterController::loadStreetPolygon(const QString& featureId) {
    if (featureId.trimmed().isEmpty()) {
        qCWarning(PlanMasterControllerLog) << "Invalid featureId input: Empty";
        emit errorMessage(tr("잘못된 주소: 유효한 지적도 ID를 입력하세요."));
        return;
    }

    QByteArray apiKey = qgetenv("v_world_key");
    if (apiKey.isEmpty()) {
        qCWarning(PlanMasterControllerLog) << "v_world_key is not set";
        emit errorMessage(tr("V-World API 키가 설정되지 않았습니다."));
        return;
    }

    qCDebug(PlanMasterControllerLog) << "Loading polygon for featureId (PNU):" << featureId;

    // V-World 지적도 API 호출 (LP_PA_CBND_BUBUN)
    QString cadastralUrl = QString("https://api.vworld.kr/req/data?service=data&version=2.0&request=GetFeature&format=json&size=10&page=1&geometry=true&attribute=true&crs=EPSG:4326&data=LP_PA_CBND_BUBUN&key=%1&domain=&attributeFilter=pnu=%2")
                               .arg(QString(apiKey))
                               .arg(QUrl::toPercentEncoding(featureId));
    qCDebug(PlanMasterControllerLog) << "Cadastral URL for LP_PA_CBND_BUBUN:" << cadastralUrl;

    QNetworkAccessManager* cadastralManager = new QNetworkAccessManager(this);
    connect(cadastralManager, &QNetworkAccessManager::finished, cadastralManager, &QObject::deleteLater);
    QNetworkReply* cadastralReply = cadastralManager->get(QNetworkRequest(QUrl(cadastralUrl)));
    connect(cadastralReply, &QNetworkReply::finished, this, [this, cadastralReply, featureId]() {
        cadastralReply->deleteLater();

        if (cadastralReply->error() != QNetworkReply::NoError) {
            qCWarning(PlanMasterControllerLog) << "Cadastral API error:" << cadastralReply->errorString();
            emit errorMessage(tr("지적도 데이터를 가져오지 못했습니다: %1").arg(cadastralReply->errorString()));
            return;
        }

        QByteArray cadastralResponseData = cadastralReply->readAll();
        qCDebug(PlanMasterControllerLog) << "Cadastral API response:" << QString(cadastralResponseData);

        QJsonDocument cadastralDoc = QJsonDocument::fromJson(cadastralResponseData);
        if (cadastralDoc.isNull() || !cadastralDoc.isObject()) {
            qCWarning(PlanMasterControllerLog) << "Invalid JSON response from cadastral API";
            emit errorMessage(tr("지적도 API 응답을 파싱할 수 없습니다."));
            return;
        }

        QJsonObject cadastralResponse = cadastralDoc.object().value("response").toObject();
        if (cadastralResponse.value("status").toString() != "OK") {
            qCWarning(PlanMasterControllerLog) << "Cadastral API status not OK:" << cadastralResponse.value("status").toString();
            emit errorMessage(tr("지적도 데이터를 찾을 수 없습니다."));
            return;
        }

        QJsonArray features = cadastralResponse.value("result").toObject().value("featureCollection").toObject().value("features").toArray();
        if (features.isEmpty()) {
            qCWarning(PlanMasterControllerLog) << "No features found in cadastral data for featureId:" << featureId;
            emit errorMessage(tr("지정된 지번에서 지적도 데이터를 찾을 수 없습니다."));
            return;
        }

        QJsonObject firstFeature = features[0].toObject();
        QJsonObject properties = firstFeature.value("properties").toObject();
        QString pnu = properties.value("pnu").toString();
        QString jimok = properties.value("jimok_nm").toString();
        qCDebug(PlanMasterControllerLog) << "Cadastral data - PNU:" << pnu << "Land Type:" << jimok;

        QJsonObject geometry = firstFeature.value("geometry").toObject();
        if (!geometry.contains("type") || !geometry.contains("coordinates")) {
            qCWarning(PlanMasterControllerLog) << "Invalid geometry in cadastral data";
            emit errorMessage(tr("유효하지 않은 지적도 데이터입니다."));
            return;
        }

        QString geomType = geometry.value("type").toString();
        QJsonArray coordinates = geometry.value("coordinates").toArray();
        QList<QGeoCoordinate> polygonPoints;
        if (geomType == "Polygon") {
            QJsonArray polygonRing = coordinates[0].toArray();
            qCDebug(PlanMasterControllerLog) << "Processing Polygon with" << polygonRing.size() << "boundary points";
            for (const QJsonValue& pointValue : polygonRing) {
                QJsonArray point = pointValue.toArray();
                if (point.size() >= 2) {
                    double longitude = point[0].toDouble();
                    double latitude = point[1].toDouble();
                    QGeoCoordinate coord(latitude, longitude);
                    if (coord.isValid()) {
                        polygonPoints.append(coord);
                    }
                }
            }
        } else if (geomType == "MultiPolygon") {
            QJsonArray firstPolygon = coordinates[0].toArray();
            QJsonArray polygonRing = firstPolygon[0].toArray();
            qCDebug(PlanMasterControllerLog) << "Processing MultiPolygon with" << polygonRing.size() << "boundary points";
            for (const QJsonValue& pointValue : polygonRing) {
                QJsonArray point = pointValue.toArray();
                if (point.size() >= 2) {
                    double longitude = point[0].toDouble();
                    double latitude = point[1].toDouble();
                    QGeoCoordinate coord(latitude, longitude);
                    if (coord.isValid()) {
                        polygonPoints.append(coord);
                    }
                }
            }
        } else {
            qCWarning(PlanMasterControllerLog) << "Unsupported geometry type:" << geomType;
            emit errorMessage(tr("지원되지 않는 지적도 형상입니다: %1").arg(geomType));
            return;
        }

        if (polygonPoints.size() < 3) {
            qCWarning(PlanMasterControllerLog) << "Insufficient boundary points for survey creation:" << polygonPoints.size();
            emit errorMessage(tr("Survey 생성에 필요한 경계점이 부족합니다."));
            return;
        }

        qCDebug(PlanMasterControllerLog) << "Successfully parsed" << polygonPoints.size() << "valid boundary points";

        // Survey 미션 생성
        int nextIndex = _missionController.currentPlanViewVIIndex() + 1;
        _missionController.insertComplexMissionItem("Survey", polygonPoints.first(), nextIndex, true);

        VisualMissionItem* newSurveyItem = qobject_cast<VisualMissionItem*>(_missionController.visualItems()->get(nextIndex));
        if (newSurveyItem) {
            SurveyComplexItem* surveyItem = qobject_cast<SurveyComplexItem*>(newSurveyItem);
            if (surveyItem) {
                QGCMapPolygon* polygon = surveyItem->surveyAreaPolygon();
                if (polygon) {
                    polygon->setPath(polygonPoints);
                    qCDebug(PlanMasterControllerLog) << "Successfully set survey area polygon with" << polygonPoints.size() << "points";
                } else {
                    qCWarning(PlanMasterControllerLog) << "Failed to get surveyAreaPolygon from SurveyComplexItem";
                    emit errorMessage(tr("Survey 영역 폴리곤을 설정하지 못했습니다."));
                }
            } else {
                qCWarning(PlanMasterControllerLog) << "Failed to cast VisualMissionItem to SurveyComplexItem";
                emit errorMessage(tr("Survey 미션 아이템으로 변환하지 못했습니다."));
            }

            qCDebug(PlanMasterControllerLog) << "Successfully created Survey mission with" << polygonPoints.size() << "boundary points";
            emit errorMessage(tr("Survey 미션이 생성되었습니다: PNU %1").arg(pnu));

            // 폴리곤 중심으로 지도 이동
            QGeoCoordinate center = calculatePolygonCenter(polygonPoints);
            if (center.isValid()) {
                double maxDistance = 0.0;
                for (int i = 0; i < polygonPoints.size(); ++i) {
                    for (int j = i + 1; j < polygonPoints.size(); ++j) {
                        double distance = polygonPoints[i].distanceTo(polygonPoints[j]);
                        maxDistance = qMax(maxDistance, distance);
                    }
                }
                int zoomLevel = maxDistance > 1000 ? 13 : maxDistance > 500 ? 14 : 15;
                qCDebug(PlanMasterControllerLog) << "Emitting panAndZoomMap signal to QML with lat:" << center.latitude() << "lon:" << center.longitude() << "zoom:" << zoomLevel;
                emit panAndZoomMap(center.latitude(), center.longitude(), zoomLevel);
            } else {
                qCWarning(PlanMasterControllerLog) << "Invalid polygon center for featureId:" << featureId;
                emit errorMessage(tr("폴리곤 중심을 계산할 수 없습니다."));
            }
            emit planReadyForViewing();
        } else {
            qCWarning(PlanMasterControllerLog) << "Failed to create Survey mission item";
            emit errorMessage(tr("Survey 미션 생성에 실패했습니다."));
        }
    });
}





