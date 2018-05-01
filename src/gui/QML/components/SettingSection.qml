import QtQuick 2.6
import QtQuick.Layouts 1.1

ColumnLayout{
    property alias isNotFirst : line.visible
    property alias text : text.text

    spacing: Units.dp(20)
    Layout.fillWidth: true

    ColumnLayout{
        Rectangle {
            id: line
            Layout.fillWidth: true
            height: 1

            color: "transparent"
            border.color: "lightgrey"
        }

        Text {
            id: text
            color: "dimgrey"
        }
    }
}
