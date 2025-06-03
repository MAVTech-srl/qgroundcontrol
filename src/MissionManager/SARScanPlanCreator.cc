#include "SARScanPlanCreator.h"
#include "PlanMasterController.h"
#include "QGCMAVLink.h"
#include "CorridorScanComplexItem.h"

SARScanPlanCreator::SARScanPlanCreator(PlanMasterController* planMasterController)
    : PlanCreator(planMasterController, "SAR Scan", QStringLiteral("/qmlimages/PlanCreator/SARScanPlanCreator.png"), QGCMAVLink::allVehicleClasses())
{

}

void SARScanPlanCreator::createPlan(const QGeoCoordinate& mapCenterCoord)
{
    _planMasterController->removeAll();
    VisualMissionItem* takeoffItem = _missionController->insertTakeoffItem(mapCenterCoord, -1);
    _missionController->setGlobalAltitudeFrame(QGroundControlQmlGlobal::AltitudeFrameCalcAboveTerrain);

    CorridorScanComplexItem* scanItem = qobject_cast<CorridorScanComplexItem*>(_missionController->insertComplexMissionItem("SAR Scan", mapCenterCoord, -1, true));
    scanItem->corridorPolyline()->setDefaultDecimation(40);
    scanItem->corridorWidth()->setRawValue(20); // Hard-code corridor width to clearly see terrain conflicts
    // Hard-code camera to GoPro Hero 4 to ensure ultra-wide FOV for single-pass coverage
    scanItem->cameraCalc()->setCameraBrand("GoPro");
    scanItem->cameraCalc()->setCameraModel("Hero 4");
    _missionController->setCurrentPlanViewSeqNum(takeoffItem->sequenceNumber(), true);
}
