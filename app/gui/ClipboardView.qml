import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

import ComputerManager 1.0

Flickable {
    id: page
    objectName: qsTr("Portapapeles")
    contentWidth: width
    contentHeight: content.height + 40
    clip: true

    ScrollBar.vertical: ScrollBar {}

    ColumnLayout {
        id: content
        width: Math.min(page.width - 40, 900)
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 24
        spacing: 18

        Label {
            text: qsTr("Portapapeles compartido")
            font.pixelSize: 30
            font.bold: true
            color: "#f8fafc"
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: "#cbd5e1"
            text: qsTr("Sincroniza texto, imágenes y copias de archivos únicamente con la computadora remota activa y autorizada. El streaming continúa funcionando aunque el agente remoto no esté disponible.")
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: statusColumn.implicitHeight + 32
            radius: 12
            color: "#172033"
            border.color: "#2d3b55"

            ColumnLayout {
                id: statusColumn
                anchors.fill: parent
                anchors.margins: 16
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    Label { text: qsTr("Sincronización"); font.bold: true; Layout.fillWidth: true }
                    Switch {
                        checked: ComputerManager.clipboardManager.enabled
                        onToggled: ComputerManager.clipboardManager.enabled = checked
                    }
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: ComputerManager.clipboardManager.status
                    color: "#7dd3fc"
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    visible: ComputerManager.clipboardManager.lastError.length > 0
                    text: ComputerManager.clipboardManager.lastError
                    color: "#fca5a5"
                }
                Button {
                    text: qsTr("Reconectar canal")
                    enabled: ComputerManager.clipboardManager.enabled
                    onClicked: ComputerManager.clipboardManager.reconnect()
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: transferColumn.implicitHeight + 32
            radius: 12
            color: "#172033"
            border.color: "#2d3b55"
            visible: ComputerManager.clipboardManager.transferActive ||
                     ComputerManager.clipboardManager.transferDescription.length > 0

            ColumnLayout {
                id: transferColumn
                anchors.fill: parent
                anchors.margins: 16
                Label { text: qsTr("Transferencia"); font.bold: true }
                Label { text: ComputerManager.clipboardManager.transferDescription; Layout.fillWidth: true; wrapMode: Text.Wrap }
                ProgressBar {
                    Layout.fillWidth: true
                    from: 0; to: 100
                    value: ComputerManager.clipboardManager.transferProgress
                    visible: ComputerManager.clipboardManager.transferActive
                }
                Button {
                    text: qsTr("Cancelar")
                    visible: ComputerManager.clipboardManager.transferActive
                    onClicked: ComputerManager.clipboardManager.cancelTransfer()
                }
            }
        }

        Label { text: qsTr("Solicitudes pendientes"); font.pixelSize: 20; font.bold: true }
        Label {
            visible: pendingList.count === 0
            text: qsTr("No hay equipos esperando autorización.")
            color: "#94a3b8"
        }
        Repeater {
            id: pendingList
            model: ComputerManager.clipboardManager.pendingPeers
            delegate: Rectangle {
                Layout.fillWidth: true
                implicitHeight: pendingRow.implicitHeight + 24
                radius: 10
                color: "#172033"
                RowLayout {
                    id: pendingRow
                    anchors.fill: parent
                    anchors.margins: 12
                    Label { text: modelData; Layout.fillWidth: true; wrapMode: Text.Wrap }
                    Button { text: qsTr("Autorizar"); onClicked: ComputerManager.clipboardManager.authorizePending(index) }
                    Button { text: qsTr("Rechazar"); onClicked: ComputerManager.clipboardManager.rejectPending(index) }
                }
            }
        }

        Label { text: qsTr("Equipos autorizados"); font.pixelSize: 20; font.bold: true }
        Label {
            visible: authorizedList.count === 0
            text: qsTr("Todavía no hay equipos autorizados para compartir el portapapeles.")
            color: "#94a3b8"
        }
        Repeater {
            id: authorizedList
            model: ComputerManager.clipboardManager.authorizedPeers
            delegate: Rectangle {
                Layout.fillWidth: true
                implicitHeight: authorizedRow.implicitHeight + 24
                radius: 10
                color: "#172033"
                RowLayout {
                    id: authorizedRow
                    anchors.fill: parent
                    anchors.margins: 12
                    Label { text: modelData; Layout.fillWidth: true; wrapMode: Text.Wrap }
                    Button { text: qsTr("Revocar"); onClicked: ComputerManager.clipboardManager.revokeAuthorized(index) }
                }
            }
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: "#94a3b8"
            text: qsTr("Para autorizar dos instalaciones, conéctese al escritorio remoto, abra SIAC en ambos equipos y compare el código corto mostrado antes de aceptar.")
        }
    }
}
