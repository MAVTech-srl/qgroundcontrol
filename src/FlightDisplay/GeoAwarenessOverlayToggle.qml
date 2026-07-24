/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

import QGroundControl
import QGroundControl.Controls
import QGroundControl.FlightDisplay

ToolStripAction {
    id:         airspaceOverlayToggle
    text:       qsTr("Overlay")
    enabled:    QGroundControl.geoZoneManager.count > 0
    iconSource: QGroundControl.settingsManager.flightMapSettings.showAirspaceOverlay.rawValue ? "qrc:/InstrumentValueIcons/view-hide.svg" : "qrc:/InstrumentValueIcons/view-show.svg"

    onTriggered: {
        QGroundControl.settingsManager.flightMapSettings.showAirspaceOverlay.rawValue = !QGroundControl.settingsManager.flightMapSettings.showAirspaceOverlay.rawValue
    }
}
