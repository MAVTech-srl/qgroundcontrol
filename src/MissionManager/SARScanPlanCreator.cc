/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "SARScanPlanCreator.h"
#include "PlanMasterController.h"
#include "CorridorScanComplexItem.h"

SARScanPlanCreator::SARScanPlanCreator(PlanMasterController* planMasterController, QObject* parent)
    : PlanCreator(planMasterController, "SAR Scan", QStringLiteral("/qmlimages/PlanCreator/SARScanPlanCreator.png"), parent)
{

}

void SARScanPlanCreator::createPlan(const QGeoCoordinate& mapCenterCoord)
{
    _planMasterController->removeAll();
    VisualMissionItem* takeoffItem = _missionController->insertTakeoffItem(mapCenterCoord, -1);
    CorridorScanComplexItem* scanItem = qobject_cast<CorridorScanComplexItem*>(_missionController->insertComplexMissionItem("SAR Scan", mapCenterCoord, -1, true));
    scanItem->corridorPolyline()->setDecimationSlider(50);
    _missionController->insertLandItem(mapCenterCoord, -1);
}
