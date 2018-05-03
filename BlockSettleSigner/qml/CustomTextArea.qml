import QtQuick 2.9
import QtQuick.Controls 2.3
import QtQuick.Controls.Styles 1.4

TextArea {
    horizontalAlignment: Text.AlignHLeft
    font.pixelSize: 11
    color: "white"

    background: Rectangle {
        implicitWidth: 200
        implicitHeight: 50
        color:"transparent"
        border.color: "#757E83"
    }
}
