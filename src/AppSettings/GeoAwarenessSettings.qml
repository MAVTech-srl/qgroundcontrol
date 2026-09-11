import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import QGroundControl
import QGroundControl.Controls

/// Geo Awareness file management.
/// Self-contained component including all dialogs needed for Geo Awareness operations.
Item {
    id: root
    implicitHeight: mainLayout.implicitHeight

    property var _appSettings: QGroundControl.settingsManager.appSettings
    property Fact              _mapAirspaceJsonFilePath: QGroundControl.settingsManager.flightMapSettings.airspaceFilePath
    property Fact              _showAirspaceOverlayFact: QGroundControl.settingsManager.flightMapSettings.showAirspaceOverlay

    ColumnLayout {
        id:    mainLayout
        width: parent.width

        SettingsGroupLayout {
            Layout.fillWidth:   true
            heading:            qsTr("Geo-Awareness")
            headingDescription: qsTr("EASA Airspace Restrictions")

            ColumnLayout {
                Layout.fillWidth:   true
                spacing:            ScreenTools.defaultFontPixelWidth

                RowLayout{
                    Layout.fillWidth:   true
                    spacing:            ScreenTools.defaultFontPixelWidth

                    QGCLabel {
                        wrapMode:   Text.WordWrap
                        visible:    true
                        text:       qsTr("Airspace JSON File:")
                    }

                    QGCTextField {
                        id:                 airspaceFileTextField
                        height:             ScreenTools.defaultFontPixelWidth * 4.5
                        unitsLabel:         ""
                        showUnits:          false
                        visible:            true
                        Layout.fillWidth:   true
                        readOnly:           true
                        text:               _mapAirspaceJsonFilePath.rawValue == "" ? qsTr("Please select a JSON file") : _mapAirspaceJsonFilePath.rawValue
                    }
                }

                RowLayout{
                    Layout.alignment:   Qt.AlignRight
                    spacing:            ScreenTools.defaultFontPixelWidth

                    QGCButton {
                        text: qsTr("Clear")

                        onClicked: {
                            _mapAirspaceJsonFilePath.value = ""
                            fileDialogAirspace.folder = root._appSettings.missionSavePath
                            QGroundControl.geoZoneManager.loadFromFile("")
                            QGroundControl.geoZoneManager.clipAllZones()
                        }
                    }

                    QGCButton {
                        text: qsTr("Select File")

                        onClicked: {
                            if (Qt.platform.os === "android") {
                                fileDialogAirspace.folder = root._appSettings.missionSavePath;
                            } else {
                                const filename = _mapAirspaceJsonFilePath.rawValue;

                                if (filename && !filename.startsWith("content://")) {
                                    const found = filename.match(/(.*)[\/\\]/);
                                    if (found) {
                                        fileDialogAirspace.folder = found[1];
                                    }
                                } else {
                                    fileDialogAirspace.folder = root._appSettings.missionSavePath;
                                }
                            }

                            fileDialogAirspace.openForLoad()
                        }

                        QGCFileDialog {
                            id:             fileDialogAirspace
                            folder:         root._appSettings.missionSavePath
                            nameFilters:    [qsTr("JSON Files (*.json)")]
                            title:          qsTr("Select Airspace File")

                            onAcceptedForLoad: (file) => {
                                console.warn("Selected airspace file: " + file)
                                airspaceFileTextField.text = file
                                _mapAirspaceJsonFilePath.value = airspaceFileTextField.text
                                QGroundControl.geoZoneManager.loadFromFile(airspaceFileTextField.text)
                                QGroundControl.geoZoneManager.clipAllZones()
                            }
                        }
                    }
                }
            }
        }
    }
}
