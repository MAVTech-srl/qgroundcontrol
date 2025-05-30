/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/


#include "PX4TuningComponent.h"
#include "QGCMAVLink.h"
#include "Vehicle.h"

PX4TuningComponent::PX4TuningComponent(Vehicle* vehicle, AutoPilotPlugin* autopilot, QObject* parent)
    : VehicleComponent(vehicle, autopilot, AutoPilotPlugin::UnknownVehicleComponent, parent)
    , _name(tr("PID Tuning"))
{
}

QString PX4TuningComponent::name(void) const
{
    return _name;
}

QString PX4TuningComponent::description(void) const
{
    return tr("Tuning Setup is used to tune the flight controllers.");
}

QString PX4TuningComponent::iconResource(void) const
{
    return "/qmlimages/TuningComponentIcon.png";
}

bool PX4TuningComponent::requiresSetup(void) const
{
    return false;
}

bool PX4TuningComponent::setupComplete(void) const
{
    return true;
}

QStringList PX4TuningComponent::setupCompleteChangedTriggerList(void) const
{
    return QStringList();
}

QUrl PX4TuningComponent::setupSource(void) const
{
    QString qmlFile;

    switch (_vehicle->vehicleType()) {
        case MAV_TYPE_FIXED_WING:
            qmlFile = "qrc:/qml/QGroundControl/AutoPilotPlugins/PX4/PX4TuningComponentPlane.qml";
            break;
        case MAV_TYPE_QUADROTOR:
        case MAV_TYPE_COAXIAL:
        case MAV_TYPE_HELICOPTER:
        case MAV_TYPE_HEXAROTOR:
        case MAV_TYPE_OCTOROTOR:
        case MAV_TYPE_TRICOPTER:
            qmlFile = "qrc:/qml/QGroundControl/AutoPilotPlugins/PX4/PX4TuningComponentCopter.qml";
            break;
        case MAV_TYPE_VTOL_TAILSITTER_DUOROTOR:
        case MAV_TYPE_VTOL_TAILSITTER_QUADROTOR:
        case MAV_TYPE_VTOL_TILTROTOR:
        case MAV_TYPE_VTOL_FIXEDROTOR:
        case MAV_TYPE_VTOL_TAILSITTER:
        case MAV_TYPE_VTOL_TILTWING:
        case MAV_TYPE_VTOL_RESERVED5:
            qmlFile = "qrc:/qml/QGroundControl/AutoPilotPlugins/PX4/PX4TuningComponentVTOL.qml";
            break;
        default:
            break;
    }

    return QUrl::fromUserInput(qmlFile);
}

QUrl PX4TuningComponent::summaryQmlSource(void) const
{
    return QUrl();
}
