import QtQuick
import QtQuick.Controls

import QGroundControl
import QGroundControl.ScreenTools
import QGroundControl.Controls

// Path decimation section for TransectStyleComplexItems
Column {
    // The following properties must be available up the hierarchy chain
    //property var    missionItem       ///< Mission Item for editor

    QGCLabel {
        text: decimationSlider.value.toFixed(0) + " m"
        font.pointSize: ScreenTools.largeFontPointSize
        anchors.horizontalCenter: parent.horizontalCenter
    }

    QGCSlider {
        id: decimationSlider
        anchors.horizontalCenter: parent.horizontalCenter
        width: parent.width

        from: 0
        to: 100
        stepSize: 5
        value: missionItem.corridorPolyline.sliderValue

        onPressedChanged: {
            if (!pressed) { // only run code when slider is released, to save on performance
                //console.log("New Slider value: ", value)
                missionItem.corridorPolyline.setDecimationSlider(value)
                missionItem.corridorPolyline.sliderValue = value
            }
        }
    }
}
