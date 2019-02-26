import QtQuick 2.9
import QtQuick.Layouts 1.0
import QtQuick.Controls 2.2

import "../StyledControls"

CustomTitleDialogWindow {
    id: root

    property string walletId
    property string walletName
    property bool   isRootWallet
    property string rootName
    property bool   backup: chkBackup.checked

    implicitWidth: 400
    implicitHeight: 250
    focus: true
    title: qsTr("Delete Wallet")
    rejectable: true


    cContentItem: ColumnLayout {       
        id: mainLayout
        spacing: 10

        RowLayout {
            spacing: 5
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            width: parent.width

            CustomLabelValue {
                Layout.fillWidth: true
                width: parent.width
                text: isRootWallet
                        ? qsTr("Are you sure you wish to irrevocably delete the entire wallet <%1> and all associated wallet files from your computer?").arg(walletName)
                        : ( rootName.length ? qsTr("Are you sure you wish to delete leaf wallet <%1> from HD wallet <%2>?").arg(walletName).arg(rootName)
                                            : qsTr("Are you sure you wish to delete wallet <%1>?").arg(walletName) )
            }
        }

        RowLayout {
            spacing: 5
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            Column {
                Layout.fillWidth: true

                CustomCheckBox {
                    id: chkConfirm
                    text: qsTr("I understand all the risks of wallet deletion")
                }
                CustomCheckBox {
                    visible: isRootWallet
                    id: chkBackup
                    text: qsTr("Backup Wallet")
                    checked: isRootWallet
                }
            }
        }

        Rectangle {
            Layout.fillHeight: true
        }
    }

    cFooterItem: RowLayout {
        CustomButtonBar {
            Layout.fillWidth: true

            CustomButton {
                text: qsTr("Cancel")
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                onClicked: {
                    onClicked: root.close();
                }
            }

            CustomButtonPrimary {
                text: qsTr("CONFIRM")
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                enabled: chkConfirm.checked

                onClicked: {
                    acceptAnimated()
                }
            }
        }
    }

}