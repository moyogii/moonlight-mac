import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Controls.Material 2.2
import QtQuick.Layouts 1.2

import StreamingPreferences 1.0

NavigableDialog {
    id: hotkeyCaptureDialog

    property string hotkeyName: ""
    property int capturedModifiers: 0
    property int capturedScanCode: 0
    property bool hasCapturedKey: false

    signal hotkeyCaptured(int modifiers, int scanCode)

    standardButtons: Dialog.NoButton

    onOpened: {
        capturedModifiers = 0
        capturedScanCode = 0
        hasCapturedKey = false
        captureArea.forceActiveFocus()
    }

    // Convert Qt key modifiers to SDL KMOD bitmask
    function qtModifiersToSdl(qtMods) {
        var sdlMods = 0
        // On macOS, Qt swaps Ctrl and Meta:
        // Qt.ControlModifier = Cmd key (physical) = SDL KMOD_GUI
        // Qt.MetaModifier = Ctrl key (physical) = SDL KMOD_CTRL
        if (Qt.platform.os === "osx") {
            if (qtMods & Qt.ControlModifier) sdlMods |= 0x0C00  // KMOD_GUI
            if (qtMods & Qt.MetaModifier)    sdlMods |= 0x00C0  // KMOD_CTRL
        } else {
            if (qtMods & Qt.ControlModifier) sdlMods |= 0x00C0  // KMOD_CTRL
            if (qtMods & Qt.MetaModifier)    sdlMods |= 0x0C00  // KMOD_GUI
        }
        if (qtMods & Qt.AltModifier)     sdlMods |= 0x0300  // KMOD_ALT
        if (qtMods & Qt.ShiftModifier)   sdlMods |= 0x0003  // KMOD_SHIFT
        return sdlMods
    }

    // Convert Qt key code to SDL scancode
    // Returns 0 if the key is not supported
    function qtKeyToSdlScancode(qtKey) {
        // A-Z: Qt.Key_A=0x41..Qt.Key_Z=0x5A -> SDL_SCANCODE_A=4..SDL_SCANCODE_Z=29
        if (qtKey >= Qt.Key_A && qtKey <= Qt.Key_Z) {
            return 4 + (qtKey - Qt.Key_A)
        }
        // 0-9: Qt.Key_0=0x30..Qt.Key_9=0x39 -> SDL_SCANCODE_1=30..SDL_SCANCODE_9=38, SDL_SCANCODE_0=39
        if (qtKey >= Qt.Key_1 && qtKey <= Qt.Key_9) {
            return 30 + (qtKey - Qt.Key_1)
        }
        if (qtKey === Qt.Key_0) {
            return 39
        }
        // F1-F12: Qt.Key_F1=0x01000030..Qt.Key_F12=0x0100003B -> SDL_SCANCODE_F1=58..SDL_SCANCODE_F12=69
        if (qtKey >= Qt.Key_F1 && qtKey <= Qt.Key_F12) {
            return 58 + (qtKey - Qt.Key_F1)
        }
        return 0
    }

    function isModifierKey(qtKey) {
        return qtKey === Qt.Key_Control || qtKey === Qt.Key_Shift ||
               qtKey === Qt.Key_Alt || qtKey === Qt.Key_Meta
    }

    ColumnLayout {
        spacing: 15

        Label {
            text: qsTr("Press a key combination for: %1").arg(hotkeyCaptureDialog.hotkeyName)
            font.bold: true
            wrapMode: Label.WordWrap
            Layout.maximumWidth: 350
        }

        Label {
            text: qsTr("Hold 0-4 modifiers (Ctrl, Alt, Shift, Cmd/Super) then press a key (A-Z, 0-9, F1-F12).")
            font.pointSize: 12
            wrapMode: Label.WordWrap
            Layout.maximumWidth: 350
        }

        Rectangle {
            id: captureArea
            Layout.fillWidth: true
            Layout.preferredHeight: 50
            color: activeFocus ? "#3a3a5a" : "#2a2a3a"
            border.color: activeFocus ? Material.accent : "#555"
            border.width: 2
            radius: 4
            focus: true

            Label {
                anchors.centerIn: parent
                text: {
                    if (!hotkeyCaptureDialog.hasCapturedKey && hotkeyCaptureDialog.capturedModifiers === 0) {
                        return qsTr("Waiting for input...")
                    }
                    return StreamingPreferences.hotkeyToString(hotkeyCaptureDialog.capturedModifiers,
                                                               hotkeyCaptureDialog.capturedScanCode)
                }
                font.pointSize: 14
                color: hotkeyCaptureDialog.hasCapturedKey ? "white" : "#aaa"
            }

            Keys.onPressed: {
                event.accepted = true

                if (hotkeyCaptureDialog.isModifierKey(event.key)) {
                    // Only update modifiers display while waiting for main key
                    if (!hotkeyCaptureDialog.hasCapturedKey) {
                        hotkeyCaptureDialog.capturedModifiers = hotkeyCaptureDialog.qtModifiersToSdl(event.modifiers)
                        hotkeyCaptureDialog.capturedScanCode = 0
                    }
                    return
                }

                var scanCode = hotkeyCaptureDialog.qtKeyToSdlScancode(event.key)
                if (scanCode === 0) {
                    // Unsupported key, ignore silently
                    return
                }

                hotkeyCaptureDialog.capturedModifiers = hotkeyCaptureDialog.qtModifiersToSdl(event.modifiers)
                hotkeyCaptureDialog.capturedScanCode = scanCode
                hotkeyCaptureDialog.hasCapturedKey = true
            }

            Keys.onReleased: {
                event.accepted = true
                // Update modifier display on release if no main key captured yet
                if (!hotkeyCaptureDialog.hasCapturedKey) {
                    hotkeyCaptureDialog.capturedModifiers = hotkeyCaptureDialog.qtModifiersToSdl(event.modifiers)
                }
            }
        }

        RowLayout {
            spacing: 10
            Layout.alignment: Qt.AlignHCenter

            Button {
                text: qsTr("Cancel")
                onClicked: {
                    hotkeyCaptureDialog.reject()
                }
            }

            Button {
                text: qsTr("Clear")
                visible: hotkeyCaptureDialog.hasCapturedKey || hotkeyCaptureDialog.capturedModifiers !== 0
                onClicked: {
                    hotkeyCaptureDialog.capturedModifiers = 0
                    hotkeyCaptureDialog.capturedScanCode = 0
                    hotkeyCaptureDialog.hasCapturedKey = false
                    captureArea.forceActiveFocus()
                }
            }

            Button {
                text: qsTr("Confirm")
                enabled: hotkeyCaptureDialog.hasCapturedKey || (hotkeyCaptureDialog.capturedModifiers === 0 && hotkeyCaptureDialog.capturedScanCode === 0)
                onClicked: {
                    hotkeyCaptureDialog.hotkeyCaptured(hotkeyCaptureDialog.capturedModifiers,
                                                       hotkeyCaptureDialog.capturedScanCode)
                    hotkeyCaptureDialog.accept()
                }
            }
        }
    }
}
