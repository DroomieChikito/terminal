
import QtQuick 2.9
import QtQuick.Layouts 1.0
import QtQuick.Controls 2.2
import com.blocksettle.QmlPdfBackup 1.0

CustomDialog {
    implicitWidth: mainWindow.width
    implicitHeight: mainWindow.height
    id: root

    InfoBanner {
        id: error
        bgColor:    "darkred"
    }

    Connections {
        target: newWalletSeed

        onUnableToPrint: {
            error.displayMessage(qsTr("No one printer is installed."))
        }

        onFailedToSave: {
            error.displayMessage(qsTr("Failed to save backup file %1").arg(filePath));
        }
    }

    FocusScope {
        anchors.fill: parent
        focus: true

        Keys.onPressed: {
            if (event.key === Qt.Key_Enter || event.key === Qt.Key_Return) {
                accept();
                event.accepted = true;
            } else if (event.key === Qt.Key_Escape) {
                reject();
                event.accepted = true;
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 10
            width: parent.width
            id: mainLayout

            CustomLabel {
                text: qsTr("Printing this sheet protects all previous and future addresses generated by this wallet!\nYou can copy the “root key” by hand if a working printer is not available.\nPlease make sure that all data lines contain 9 columns of 4 characters each.      \n\nRemember to secure your backup in an offline environment.\nThe backup is uncrypted and will allow anyone who holds it to recover the entire wallet.")
                font.pixelSize: 13;
                Layout.fillWidth: true
                id: label
                horizontalAlignment: Qt.AlignCenter
            }

            ScrollView {
                Layout.alignment: Qt.AlignCenter
                Layout.preferredWidth: mainLayout.width * 0.6
                Layout.preferredHeight: root.height - label.height - rowButtons.height - mainLayout.spacing * 2
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ScrollBar.vertical.policy: ScrollBar.AlwaysOn
                clip: true
                id: scroll
                contentWidth: width
                contentHeight: pdf.preferedHeight

                QmlPdfBackup {
                    anchors.fill: parent;
                    walletId: newWalletSeed.walletId
                    part1: newWalletSeed.part1
                    part2: newWalletSeed.part2
                    id: pdf
                }
            }

            CustomButtonBar {
                implicitHeight: childrenRect.height
                Layout.fillWidth: true
                id: rowButtons

                Flow {
                    id: buttonRow
                    spacing: 5
                    padding: 5
                    height: childrenRect.height + 10
                    width: parent.width - buttonRowLeft - 5
                    LayoutMirroring.enabled: true
                    LayoutMirroring.childrenInherit: true
                    anchors.left: parent.left   // anchor left becomes right

                    CustomButtonPrimary {
                        Layout.fillWidth: true
                        text:   qsTr("Continue")
                        onClicked: {
                            accept();
                        }
                    }

                    CustomButton {
                        Layout.fillWidth: true
                        text:   qsTr("Print")
                        onClicked: {
                            newWalletSeed.print();
                        }
                    }

                    CustomButton {
                        Layout.fillWidth: true
                        text:   qsTr("Save")
                        onClicked: {
                            newWalletSeed.save();
                        }
                    }
                }

                Flow {
                    id: buttonRowLeft
                    spacing: 5
                    padding: 5
                    height: childrenRect.height + 10


                    CustomButton {
                        Layout.fillWidth: true
                        text:   qsTr("Cancel")
                        onClicked: {
                            reject();
                        }
                    }
                }
            }
        }
    }
}
