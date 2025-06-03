#pragma once

#include "PlanCreator.h"

class SARScanPlanCreator : public PlanCreator
{
    Q_OBJECT
    
public:
    SARScanPlanCreator(PlanMasterController* planMasterController);

    Q_INVOKABLE void createPlan(const QGeoCoordinate& mapCenterCoord) final;
};
