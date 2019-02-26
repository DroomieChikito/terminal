import QtQuick 2.9
import QtQuick.Controls 2.3
import "../BsStyles"

CheckBox {
    id: control
    text: parent.text

    indicator: Rectangle {
        implicitWidth: 16
        implicitHeight: 16
        y: parent.height / 2 - height / 2
        radius: 0
        border.color: control.checked ? BSStyle.buttonsBorderColor : BSStyle.buttonsUncheckedColor
        color: "transparent"

        Rectangle {
            width: 8
            height: 8
            x: 4
            y: 4
            radius: 0
            color: control.checked ? BSStyle.buttonsPrimaryMainColor : BSStyle.buttonsUncheckedColor
            visible: control.checked
        }
    }

    contentItem: Text {
        text: control.text
        font.pixelSize: 12
        opacity: enabled ? 1.0 : 0.3
        color: control.checked ? BSStyle.textColor : BSStyle.buttonsUncheckedColor
        verticalAlignment: Text.AlignVCenter
        leftPadding: control.indicator.width + control.spacing
    }
}