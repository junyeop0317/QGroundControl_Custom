/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QObject>
#include <QtCore/QLoggingCategory>
#include <QtQmlIntegration/QtQmlIntegration>
#include <QGeoCoordinate>
#include <QVariantList>
#include "MissionController.h"
#include "GeoFenceController.h"
#include "RallyPointController.h"
#include "SurveyComplexItem.h"

Q_DECLARE_LOGGING_CATEGORY(PlanControllerLog)

class QmlObjectListModel;
class MultiVehicleManager;
class Vehicle;

class PlanMasterController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_MOC_INCLUDE("QmlObjectListModel.h")
    Q_MOC_INCLUDE("Vehicle.h")

    Q_PROPERTY(bool                     flyView                 MEMBER _flyView)
    Q_PROPERTY(Vehicle*                 controllerVehicle       READ controllerVehicle                      CONSTANT)
    Q_PROPERTY(Vehicle*                 managerVehicle          READ managerVehicle                         NOTIFY managerVehicleChanged)
    Q_PROPERTY(MissionController*       missionController       READ missionController                      CONSTANT)
    Q_PROPERTY(GeoFenceController*      geoFenceController      READ geoFenceController                     CONSTANT)
    Q_PROPERTY(RallyPointController*    rallyPointController    READ rallyPointController                   CONSTANT)
    Q_PROPERTY(bool                     offline                 READ offline                                NOTIFY offlineChanged)
    Q_PROPERTY(bool                     containsItems           READ containsItems                          NOTIFY containsItemsChanged)
    Q_PROPERTY(bool                     syncInProgress          READ syncInProgress                         NOTIFY syncInProgressChanged)
    Q_PROPERTY(bool                     dirty                   READ dirty                  WRITE setDirty  NOTIFY dirtyChanged)
    Q_PROPERTY(QString                  fileExtension           READ fileExtension                          CONSTANT)
    Q_PROPERTY(QString                  kmlFileExtension        READ kmlFileExtension                       CONSTANT)
    Q_PROPERTY(QString                  currentPlanFile         READ currentPlanFile                        NOTIFY currentPlanFileChanged)
    Q_PROPERTY(QStringList              loadNameFilters         READ loadNameFilters                        CONSTANT)
    Q_PROPERTY(QStringList              saveNameFilters         READ saveNameFilters                        CONSTANT)
    Q_PROPERTY(QmlObjectListModel*      planCreators            MEMBER _planCreators                        NOTIFY planCreatorsChanged)
    Q_PROPERTY(QVariantList             streetResults           READ streetResults                          NOTIFY streetResultsChanged)

public:
    PlanMasterController(QObject* parent = nullptr);

#ifdef QT_DEBUG
    PlanMasterController(MAV_AUTOPILOT firmwareType, MAV_TYPE vehicleType, QObject* parent = nullptr);
#endif

    ~PlanMasterController();

    Q_INVOKABLE void start(void);
    Q_INVOKABLE void startStaticActiveVehicle(Vehicle* vehicle, bool deleteWhenSendCompleted = false);
    Q_INVOKABLE int readyForSaveState(void) const { return _missionController.readyForSaveState(); }
    Q_INVOKABLE void showPlanFromManagerVehicle(void);
    static void sendPlanToVehicle(Vehicle* vehicle, const QString& filename);

    Q_INVOKABLE void loadFromVehicle(void);
    Q_INVOKABLE void sendToVehicle(void);
    Q_INVOKABLE void loadFromFile(const QString& filename);
    Q_INVOKABLE void saveToCurrent();
    Q_INVOKABLE void saveToFile(const QString& filename);
    Q_INVOKABLE void saveToKml(const QString& filename);
    Q_INVOKABLE void removeAll(void);
    Q_INVOKABLE void removeAllFromVehicle(void);
    Q_INVOKABLE void addWaypointAndZoom(double latitude, double longitude);
    Q_INVOKABLE void searchStreet(const QString& jibunText);
    Q_INVOKABLE void loadStreetPolygon(const QString& featureId);

    Q_INVOKABLE void searchAndGo(const QString& address, bool panAfterSearch);
    Q_INVOKABLE void findCadastralAndCreateSurvey(const QString& address);

    MissionController* missionController(void)     { return &_missionController; }
    GeoFenceController* geoFenceController(void)    { return &_geoFenceController; }
    RallyPointController* rallyPointController(void)  { return &_rallyPointController; }

    bool        offline         (void) const { return _offline; }
    bool        containsItems   (void) const;
    bool        syncInProgress  (void) const;
    bool        dirty           (void) const;
    void        setDirty        (bool dirty);
    QString     fileExtension   (void) const;
    QString     kmlFileExtension(void) const;
    QString     currentPlanFile (void) const { return _currentPlanFile; }
    QStringList loadNameFilters (void) const;
    QStringList saveNameFilters (void) const;
    bool        isEmpty         (void) const;
    QVariantList streetResults() const;

    void        setFlyView(bool flyView) { _flyView = flyView; }
    QJsonDocument saveToJson();
    Vehicle* controllerVehicle(void) { return _controllerVehicle; }
    Vehicle* managerVehicle(void) { return _managerVehicle; }

    static constexpr int   kPlanFileVersion =            1;
    static constexpr const char* kPlanFileType =               "Plan";
    static constexpr const char* kJsonMissionObjectKey =       "mission";
    static constexpr const char* kJsonGeoFenceObjectKey =      "geoFence";
    static constexpr const char* kJsonRallyPointsObjectKey =   "rallyPoints";

signals:
    void containsItemsChanged               (bool containsItems);
    void syncInProgressChanged              (void);
    void dirtyChanged                       (bool dirty);
    void offlineChanged                     (bool offlineEditing);
    void currentPlanFileChanged             (void);
    void planCreatorsChanged                (QmlObjectListModel* planCreators);
    void managerVehicleChanged              (Vehicle* managerVehicle);
    void promptForPlanUsageOnVehicleChange  (void);
    void suggestionsReady                   (QVariantList suggestions);
    void panAndZoomMap                      (double latitude, double longitude, int zoomLevel);
    void planReadyForViewing                (void);
    void streetResultsChanged               (void);
    void errorMessage(const QString& message); // 추가: 에러 메시지 신호

private slots:
    void _activeVehicleChanged      (Vehicle* activeVehicle);
    void _loadMissionComplete       (void);
    void _loadGeoFenceComplete      (void);
    void _loadRallyPointsComplete   (void);
    void _sendMissionComplete       (void);
    void _sendGeoFenceComplete      (void);
    void _sendRallyPointsComplete   (void);
    void _updateOverallDirty        (void);
    void _updatePlanCreatorsList    (void);

private:
    void _commonInit                (void);
    void _showPlanFromManagerVehicle(void);
    QGeoCoordinate calculatePolygonCenter(const QList<QGeoCoordinate>& polygonPoints); // 추가: 폴리곤 중심 계산 헬퍼

    MultiVehicleManager* _multiVehicleMgr =          nullptr;
    Vehicle* _controllerVehicle =        nullptr;
    Vehicle* _managerVehicle =           nullptr;
    bool                    _flyView =                  true;
    bool                    _offline =                  true;
    MissionController       _missionController;
    GeoFenceController      _geoFenceController;
    RallyPointController    _rallyPointController;
    bool                    _loadGeoFence =             false;
    bool                    _loadRallyPoints =          false;
    bool                    _sendGeoFence =             false;
    bool                    _sendRallyPoints =          false;
    QString                 _currentPlanFile;
    bool                    _deleteWhenSendCompleted =  false;
    bool                    _previousOverallDirty =     false;
    QmlObjectListModel*     _planCreators =             nullptr;
    QVariantList            _streetResults;
};
