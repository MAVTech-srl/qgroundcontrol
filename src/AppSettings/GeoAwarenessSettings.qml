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
                        text:               _mapAirspaceJsonFilePath.rawValue
                    }
                }

                RowLayout{
                    Layout.alignment:   Qt.AlignRight
                    spacing:            ScreenTools.defaultFontPixelWidth

                    QGCButton {
                        text: qsTr("Clear")

                        onClicked: {
                            airspaceFileTextField.text = "Please select a JSON file"
                            _mapAirspaceJsonFilePath.value = airspaceFileTextField.text
                        }
                    }

                    QGCButton {
                        text: qsTr("Select File")

                        onClicked: {
                            var filename = _mapAirspaceJsonFilePath.rawValue;
                            const found = filename.match(/(.*)[\/\\]/);
                            if(found){
                                filename = found[1]||''; // extracting the directory from the file path
                                fileDialogAirspace.folder = (filename[0] === "/")?(filename.slice(1)):(filename);
                            }
                            fileDialogAirspace.openForLoad()
                        }

                        QGCFileDialog {
                            id:             fileDialogAirspace
                            nameFilters:    [qsTr("JSON Files (*.json)")]
                            title:          qsTr("Select Airspace File")

                            onAcceptedForLoad: (file) => {
                                airspaceFileTextField.text = file
                                _mapAirspaceJsonFilePath.value = airspaceFileTextField.text
                                QGroundControl.geoZoneManager.loadFromFile(airspaceFileTextField.text)
                            }
                        }
                    }
                }
            }
        }
    }
}
