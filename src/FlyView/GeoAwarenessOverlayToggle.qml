import QGroundControl
import QGroundControl.Controls

ToolStripAction {
    id:         airspaceOverlayToggle
    text:       qsTr("Overlay")
    enabled:    QGroundControl.geoZoneManager.count > 0
    iconSource: QGroundControl.settingsManager.flightMapSettings.showAirspaceOverlay.rawValue ? "qrc:/InstrumentValueIcons/view-hide.svg" : "qrc:/InstrumentValueIcons/view-show.svg"

    onTriggered: {
        QGroundControl.settingsManager.flightMapSettings.showAirspaceOverlay.rawValue = !QGroundControl.settingsManager.flightMapSettings.showAirspaceOverlay.rawValue
    }
}
