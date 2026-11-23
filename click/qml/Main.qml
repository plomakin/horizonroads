import QtQuick 2.12
import QtQuick.Window 2.12
import Lomiri.Components 1.3
import QtSensors 5.11
import HorizonRoads 1.0

Window {
    id: rootWin
    visible: true
    visibility: Window.FullScreen
    // Lomiri/Mir Staged-mode fullscreen flag (0x00800000) hides the top indicator panel edge-to-edge
    flags: Qt.Window | 0x00800000
    width: Screen.width > 0 ? Screen.width : units.gu(45)
    height: Screen.height > 0 ? Screen.height : units.gu(80)
    title: "Horizon Roads 3D"
    color: "#0b0f19"

    property bool forceLandscape: true
    property bool drawerOpen: false
    property bool minimalZenUi: false
    property bool sensorReady: false

    Component.onCompleted: {
        rootWin.visibility = Window.FullScreen
        rootWin.showFullScreen()
    }

    Timer {
        interval: 300
        running: true
        repeat: false
        onTriggered: {
            rootWin.sensorReady = true
            if (rootWin.visibility !== Window.FullScreen) {
                rootWin.visibility = Window.FullScreen
            }
        }
    }

    Connections {
        target: Qt.application
        onStateChanged: {
            if (Qt.application.state !== Qt.ApplicationActive) {
                road.paused = true
            } else {
                if (rootWin.visibility !== Window.FullScreen) {
                    rootWin.visibility = Window.FullScreen
                }
                if (!rootWin.drawerOpen) {
                    road.paused = false
                }
            }
        }
    }

    Loader {
        id: accelLoader
        active: rootWin.sensorReady && !road.paused && road.tiltSteerEnabled
        sourceComponent: Component {
            Accelerometer {
                active: true
                dataRate: 60
                onReadingChanged: {
                    if (reading) {
                        road.setTiltGravity(reading.x, reading.y, reading.z, stage.rotate90)
                    }
                }
            }
        }
    }

    MainView {
        id: lomiriMain
        anchors.fill: parent
        applicationName: "horizonroads.plm"
        theme.name: "Lomiri.Components.Themes.SuruDark"
        backgroundColor: "#0b0f19"

        Item {
            id: stage
            readonly property bool rotate90: rootWin.forceLandscape && (lomiriMain.height > lomiriMain.width)
            width: rotate90 ? lomiriMain.height : lomiriMain.width
            height: rotate90 ? lomiriMain.width : lomiriMain.height
            anchors.centerIn: parent
            rotation: rotate90 ? 90 : 0
            focus: true

            Keys.onPressed: {
                if (event.isAutoRepeat) return
                if (event.key === Qt.Key_C) road.cycleCamera()
                else if (event.key === Qt.Key_R) road.cycleRadio()
                else if (event.key === Qt.Key_V) road.cycleCar()
                else if (event.key === Qt.Key_Space) road.autoDrive = !road.autoDrive
                else if (event.key === Qt.Key_Up || event.key === Qt.Key_W) road.setGasHeld(true)
                else if (event.key === Qt.Key_Down || event.key === Qt.Key_S) road.setBrakeHeld(true)
                else if (event.key === Qt.Key_Left || event.key === Qt.Key_A) road.setTouchSteer(-0.75)
                else if (event.key === Qt.Key_Right || event.key === Qt.Key_D) road.setTouchSteer(0.75)
            }
            Keys.onReleased: {
                if (event.isAutoRepeat) return
                if (event.key === Qt.Key_Up || event.key === Qt.Key_W) road.setGasHeld(false)
                else if (event.key === Qt.Key_Down || event.key === Qt.Key_S) road.setBrakeHeld(false)
                else if (event.key === Qt.Key_Left || event.key === Qt.Key_Right ||
                         event.key === Qt.Key_A || event.key === Qt.Key_D) road.setTouchSteer(0.0)
            }

            // === 3D OPENGL ES 2.0 SCENIC ROAD & COCKPIT VIEWPORT ===
            RoadViewport {
                id: road
                anchors.fill: parent
            }

            // === MANUAL DRIVING BOTTOM CONTROLS (LEFT/RIGHT TURN BUTTONS + BOTTOM BRAKE/GAS PEDALS!) ===
            Item {
                id: manualControlsLayer
                anchors.fill: parent
                visible: !road.autoDrive && !rootWin.drawerOpen && !rootWin.minimalZenUi

                property bool leftDown: false
                property bool rightDown: false
                property bool brakeDown: false
                property bool gasDown: false

                // Bottom-Left: [ ◀ ] and [ ▶ ] Touch Turn Buttons
                Row {
                    id: steerBtnRow
                    anchors.left: parent.left
                    anchors.leftMargin: units.gu(1.6)
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: units.gu(1.2)
                    spacing: units.gu(1.2)

                    Rectangle {
                        id: btnTurnLeft
                        width: units.gu(8.8)
                        height: units.gu(7.6)
                        radius: units.gu(1.4)
                        color: manualControlsLayer.leftDown ? "#cc38b6ff" : "#77121826"
                        border.width: units.dp(2)
                        border.color: manualControlsLayer.leftDown ? "#ffffff" : "#7fd4ff"

                        Label {
                            anchors.centerIn: parent
                            text: "◀"
                            font.bold: true
                            fontSize: "large"
                            color: "#ffffff"
                        }
                    }

                    Rectangle {
                        id: btnTurnRight
                        width: units.gu(8.8)
                        height: units.gu(7.6)
                        radius: units.gu(1.4)
                        color: manualControlsLayer.rightDown ? "#cc38b6ff" : "#77121826"
                        border.width: units.dp(2)
                        border.color: manualControlsLayer.rightDown ? "#ffffff" : "#7fd4ff"

                        Label {
                            anchors.centerIn: parent
                            text: "▶"
                            font.bold: true
                            fontSize: "large"
                            color: "#ffffff"
                        }
                    }
                }

                // Bottom-Right: [ BRAKE ] and [ GAS ] Pedals right at the bottom edge!
                Row {
                    id: pedalRow
                    anchors.right: parent.right
                    anchors.rightMargin: units.gu(1.6)
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: units.gu(1.2)
                    spacing: units.gu(1.2)

                    Rectangle {
                        id: btnBrake
                        width: units.gu(8.8)
                        height: units.gu(6.8)
                        anchors.bottom: parent.bottom
                        radius: units.gu(1.3)
                        color: manualControlsLayer.brakeDown ? "#ddff4d5a" : "#77161422"
                        border.width: units.dp(2)
                        border.color: manualControlsLayer.brakeDown ? "#ffffff" : "#ff7b86"

                        Label {
                            anchors.centerIn: parent
                            text: "BRAKE"
                            font.bold: true
                            fontSize: "small"
                            color: "#ffffff"
                        }
                    }

                    Rectangle {
                        id: btnGas
                        width: units.gu(10.2)
                        height: units.gu(8.4)
                        anchors.bottom: parent.bottom
                        radius: units.gu(1.3)
                        color: manualControlsLayer.gasDown ? "#dd2ee59d" : "#7712221e"
                        border.width: units.dp(2)
                        border.color: manualControlsLayer.gasDown ? "#ffffff" : "#5effb8"

                        Label {
                            anchors.centerIn: parent
                            text: "GAS"
                            font.bold: true
                            fontSize: "medium"
                            color: "#ffffff"
                        }
                    }
                }

                // Bottom-Center Quick Tilt Toggle + Zero Calibration Pills
                Row {
                    id: tiltQuickRow
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: stage.width > units.gu(58) ? units.gu(1.6) : units.gu(9.6)
                    spacing: units.gu(0.8)

                    Rectangle {
                        id: tiltTogglePill
                        width: tiltToggleLbl.implicitWidth + units.gu(2.2)
                        height: units.gu(3.6)
                        radius: units.gu(1.8)
                        color: road.tiltSteerEnabled ? "#cc16324f" : "#aa141824"
                        border.width: units.dp(1)
                        border.color: road.tiltSteerEnabled ? "#7fd4ff" : "#66ffffff"

                        Label {
                            id: tiltToggleLbl
                            anchors.centerIn: parent
                            text: road.tiltSteerEnabled ? "Tilt: ON" : "Tilt: OFF"
                            fontSize: "x-small"
                            font.bold: true
                            color: "#e2eeff"
                        }
                    }

                    Rectangle {
                        id: tiltZeroPill
                        visible: road.tiltSteerEnabled
                        width: tiltZeroLbl.implicitWidth + units.gu(2.0)
                        height: units.gu(3.6)
                        radius: units.gu(1.8)
                        color: "#aa1c2b3a"
                        border.width: units.dp(1)
                        border.color: "#7fd4ff"

                        Label {
                            id: tiltZeroLbl
                            anchors.centerIn: parent
                            text: "Zero Tilt"
                            fontSize: "x-small"
                            font.bold: true
                            color: "#bfe6ff"
                        }
                    }
                }
            }

            // === SIMULTANEOUS MULTI-TOUCH HANDLER (PEDALS + ◀/▶ BUTTONS + WINDSHIELD FREE-LOOK) ===
            MultiPointTouchArea {
                id: driveTouch
                anchors.fill: parent
                enabled: !rootWin.drawerOpen
                minimumTouchPoints: 1
                maximumTouchPoints: 5

                property int lookId: -1
                property real prevX: 0
                property real prevY: 0

                function hitItem(px, py, item) {
                    if (!item || !item.visible) return false
                    var tl = item.mapToItem(driveTouch, -units.gu(0.6), -units.gu(0.6))
                    var br = item.mapToItem(driveTouch, item.width + units.gu(0.6), item.height + units.gu(0.6))
                    var minX = Math.min(tl.x, br.x), maxX = Math.max(tl.x, br.x)
                    var minY = Math.min(tl.y, br.y), maxY = Math.max(tl.y, br.y)
                    return px >= minX && px <= maxX && py >= minY && py <= maxY
                }

                function syncManualButtons() {
                    if (road.autoDrive || rootWin.drawerOpen) {
                        manualControlsLayer.leftDown = false
                        manualControlsLayer.rightDown = false
                        manualControlsLayer.brakeDown = false
                        manualControlsLayer.gasDown = false
                        road.setTouchSteer(0.0)
                        road.setBrakeHeld(false)
                        road.setGasHeld(false)
                        return
                    }

                    var l = false, r = false, b = false, g = false
                    var pts = [pt0, pt1, pt2, pt3]
                    for (var i = 0; i < pts.length; ++i) {
                        var p = pts[i]
                        if (!p.pressed) continue
                        if (hitItem(p.x, p.y, btnTurnLeft))  l = true
                        if (hitItem(p.x, p.y, btnTurnRight)) r = true
                        if (hitItem(p.x, p.y, btnBrake))     b = true
                        if (hitItem(p.x, p.y, btnGas))       g = true
                    }
                    manualControlsLayer.leftDown = l
                    manualControlsLayer.rightDown = r
                    manualControlsLayer.brakeDown = b
                    manualControlsLayer.gasDown = g

                    road.setTouchSteer((r ? 0.82 : 0.0) - (l ? 0.82 : 0.0))
                    road.setBrakeHeld(b)
                    road.setGasHeld(g)
                }

                touchPoints: [
                    TouchPoint { id: pt0 },
                    TouchPoint { id: pt1 },
                    TouchPoint { id: pt2 },
                    TouchPoint { id: pt3 }
                ]

                onPressed: {
                    for (var i = 0; i < touchPoints.length; ++i) {
                        var tp = touchPoints[i]
                        if (!rootWin.minimalZenUi && tp.y < (road.autoDrive ? units.gu(5.4) : units.gu(10.6))) continue
                        if (!rootWin.minimalZenUi && road.autoDrive && tp.y > stage.height - units.gu(6.0)) continue

                        if (!road.autoDrive && hitItem(tp.x, tp.y, tiltTogglePill)) {
                            road.tiltSteerEnabled = !road.tiltSteerEnabled
                            continue
                        }
                        if (!road.autoDrive && hitItem(tp.x, tp.y, tiltZeroPill)) {
                            road.calibrateTiltZero()
                            continue
                        }
                        if (!road.autoDrive && (hitItem(tp.x, tp.y, btnTurnLeft) ||
                                                hitItem(tp.x, tp.y, btnTurnRight) ||
                                                hitItem(tp.x, tp.y, btnBrake) ||
                                                hitItem(tp.x, tp.y, btnGas))) {
                            continue
                        }
                        if (lookId === -1) {
                            lookId = tp.pointId
                            prevX = tp.x
                            prevY = tp.y
                        }
                    }
                    syncManualButtons()
                }

                onUpdated: {
                    for (var i = 0; i < touchPoints.length; ++i) {
                        var tp = touchPoints[i]
                        if (tp.pointId === lookId) {
                            road.addViewDrag(tp.x - prevX, tp.y - prevY)
                            prevX = tp.x
                            prevY = tp.y
                        }
                    }
                    syncManualButtons()
                }

                onReleased: {
                    for (var i = 0; i < touchPoints.length; ++i) {
                        if (touchPoints[i].pointId === lookId) lookId = -1
                    }
                    syncManualButtons()
                }

                onCanceled: {
                    lookId = -1
                    syncManualButtons()
                }
            }

            // === HIGHWAY STATE / BIOME WELCOME SIGN BANNER ===
            Rectangle {
                visible: road.bannerText.length > 0 && !rootWin.drawerOpen
                anchors.horizontalCenter: parent.horizontalCenter
                y: road.autoDrive ? units.gu(6.2) : units.gu(11.0)
                width: bannerLbl.implicitWidth + units.gu(4)
                height: units.gu(4.2)
                radius: units.gu(0.8)
                color: "#cc0b4f30"
                border.width: units.dp(2)
                border.color: "#e8f5ee"

                Label {
                    id: bannerLbl
                    anchors.centerIn: parent
                    text: road.bannerText
                    font.bold: true
                    fontSize: "small"
                    color: "#ffffff"
                }
            }

            // === MINIMALIST TOP SCENIC TELEMETRY BAR ===
            Rectangle {
                id: topBar
                visible: !rootWin.minimalZenUi
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: units.gu(5.0)
                color: "#880c101c"

                Row {
                    anchors.left: parent.left
                    anchors.leftMargin: units.gu(2.2)
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: units.gu(1.5)

                    Label {
                        text: road.speedKmh + " km/h"
                        font.bold: true
                        fontSize: "medium"
                        color: "#ffd166"
                    }
                    Label {
                        text: road.biomeTitle
                        font.bold: true
                        fontSize: "small"
                        anchors.verticalCenter: parent.verticalCenter
                        color: "#ffffff"
                    }
                    Label {
                        text: road.clockText + "  ·  " + road.distanceText
                        fontSize: "x-small"
                        anchors.verticalCenter: parent.verticalCenter
                        color: "#b8c7dc"
                    }
                }

                Row {
                    anchors.right: parent.right
                    anchors.rightMargin: units.gu(1.2)
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: units.gu(0.8)

                    Rectangle {
                        width: units.gu(5.8)
                        height: units.gu(3.4)
                        radius: units.gu(0.7)
                        color: "#44ffffff"
                        Label {
                            anchors.centerIn: parent
                            text: "HIDE UI"
                            font.bold: true
                            fontSize: "x-small"
                            color: "#ffffff"
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: rootWin.minimalZenUi = true
                        }
                    }

                    Rectangle {
                        width: units.gu(4.5)
                        height: units.gu(3.4)
                        radius: units.gu(0.7)
                        color: "#4438b6ff"
                        border.width: units.dp(1)
                        border.color: "#7fd4ff"
                        Label {
                            anchors.centerIn: parent
                            text: "90°"
                            font.bold: true
                            fontSize: "x-small"
                            color: "#ffffff"
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: rootWin.forceLandscape = !rootWin.forceLandscape
                        }
                    }

                    Rectangle {
                        width: units.gu(9.0)
                        height: units.gu(3.4)
                        radius: units.gu(0.7)
                        color: "#ccff8c3b"
                        Label {
                            anchors.centerIn: parent
                            text: "GARAGE"
                            font.bold: true
                            fontSize: "x-small"
                            color: "#11131a"
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: rootWin.drawerOpen = !rootWin.drawerOpen
                        }
                    }
                }
            }

            // Tiny corner restore button when in pure unobstructed Zen Cinema UI mode!
            Rectangle {
                visible: rootWin.minimalZenUi
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.margins: units.gu(1.2)
                width: units.gu(6.5)
                height: units.gu(3.2)
                radius: units.gu(1.6)
                color: "#550c101c"
                Label {
                    anchors.centerIn: parent
                    text: "SHOW UI"
                    fontSize: "x-small"
                    color: "#ddffffff"
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: rootWin.minimalZenUi = false
                }
            }

            // === QUICK SWITCH DOCK (Bottom in Zen Mode; Top sub-bar in Manual Mode so bottom edge stays free for Pedals & Turn Buttons!) ===
            Rectangle {
                id: bottomDock
                visible: !rootWin.minimalZenUi && !rootWin.drawerOpen
                anchors.horizontalCenter: parent.horizontalCenter
                y: road.autoDrive ? (parent.height - height - units.gu(0.8)) : (topBar.height + units.gu(0.5))
                width: dockRow.implicitWidth + units.gu(2.0)
                height: units.gu(4.4)
                radius: units.gu(2.2)
                color: "#aa0e1320"
                border.width: units.dp(1)
                border.color: "#44ffffff"

                Row {
                    id: dockRow
                    anchors.centerIn: parent
                    spacing: units.gu(0.8)

                    // 1) ZEN AUTO-DRIVE vs TILT STEER Toggle!
                    Rectangle {
                        width: units.gu(12.2)
                        height: units.gu(3.4)
                        radius: units.gu(1.7)
                        color: road.autoDrive ? "#cc2ec4b6" : "#ccff8c3b"
                        Label {
                            anchors.centerIn: parent
                            text: road.autoDrive ? "ZEN CRUISE: ON" : "STEER: MANUAL"
                            font.bold: true
                            fontSize: "x-small"
                            color: "#0e121a"
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                road.autoDrive = !road.autoDrive
                                if (!road.autoDrive) road.calibrateTiltZero()
                            }
                        }
                    }

                    // 2) Camera View Switch (Wheel / 3rd Person / Hood)
                    Rectangle {
                        width: units.gu(9.4)
                        height: units.gu(3.4)
                        radius: units.gu(1.7)
                        color: "#33ffffff"
                        Label {
                            anchors.centerIn: parent
                            text: road.cameraMode === 0 ? "VIEW: WHEEL" : (road.cameraMode === 1 ? "VIEW: 3RD" : "VIEW: HOOD")
                            font.bold: true
                            fontSize: "x-small"
                            color: "#ffffff"
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: road.cycleCamera()
                        }
                    }

                    // 3) Car Quick Cycle
                    Rectangle {
                        width: units.gu(9.8)
                        height: units.gu(3.4)
                        radius: units.gu(1.7)
                        color: "#33ffffff"
                        Label {
                            anchors.centerIn: parent
                            text: road.carIndex === 0 ? "CAR: '68 V8" : (road.carIndex === 1 ? "CAR: WAGON" : "CAR: GT-RS")
                            font.bold: true
                            fontSize: "x-small"
                            color: "#ffd166"
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: road.cycleCar()
                        }
                    }

                    // 4) FM Synth Radio Station
                    Rectangle {
                        width: units.gu(12.8)
                        height: units.gu(3.4)
                        radius: units.gu(1.7)
                        color: "#33ffffff"
                        Label {
                            anchors.centerIn: parent
                            text: road.radioStationName
                            font.bold: true
                            fontSize: "x-small"
                            color: "#8ce8ff"
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: road.cycleRadio()
                        }
                    }

                    // 5) Wipers Toggle
                    Rectangle {
                        width: units.gu(6.8)
                        height: units.gu(3.4)
                        radius: units.gu(1.7)
                        color: road.wipers ? "#8838b6ff" : "#22ffffff"
                        Label {
                            anchors.centerIn: parent
                            text: "WIPERS"
                            font.bold: true
                            fontSize: "x-small"
                            color: "#ffffff"
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: road.wipers = !road.wipers
                        }
                    }
                }
            }

            // === LOMIRI GARAGE, BIOME, TIME-OF-DAY & WEATHER PANEL ===
            Rectangle {
                id: garageOverlay
                visible: rootWin.drawerOpen
                anchors.fill: parent
                color: "#d80b0f1a"

                Flickable {
                    anchors.fill: parent
                    anchors.margins: units.gu(1.8)
                    contentWidth: width
                    contentHeight: panelCol.implicitHeight + units.gu(3)
                    clip: true

                    Column {
                        id: panelCol
                        width: Math.min(parent.width, units.gu(58))
                        anchors.horizontalCenter: parent.horizontalCenter
                        spacing: units.gu(1.2)

                        Row {
                            width: parent.width
                            Label {
                                text: "HORIZON ROADS 3D  ·  GARAGE & WORLD"
                                font.bold: true
                                fontSize: "large"
                                color: "#ffd166"
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Item { width: units.gu(2); height: 1 }
                            Button {
                                text: "BACK TO ROAD"
                                color: LomiriColors.green
                                onClicked: rootWin.drawerOpen = false
                            }
                        }

                        // 1. Choose Your Car (Classic / Comfy / Fun!)
                        Label { text: "1. Choose Car & Cockpit"; font.bold: true; fontSize: "small"; color: "#8ce8ff" }
                        Row {
                            spacing: units.gu(1.0)
                            Repeater {
                                model: [
                                    { idx: 0, title: "'68 Cruiser (Classic)" },
                                    { idx: 1, title: "'94 Wagon (Comfy)" },
                                    { idx: 2, title: "Apex GT-RS (Fun)" }
                                ]
                                delegate: Button {
                                    width: units.gu(18.0)
                                    text: modelData.title
                                    color: road.carIndex === modelData.idx ? "#e07a2b" : "#232a3b"
                                    onClicked: road.carIndex = modelData.idx
                                }
                            }
                        }

                        // 2. Choose Biome or Endless State-to-State Road Trip!
                        Label { text: "2. Region / Biome"; font.bold: true; fontSize: "small"; color: "#8ce8ff" }
                        Flow {
                            width: parent.width
                            spacing: units.gu(0.8)
                            Repeater {
                                model: [
                                    { b: -1, title: "Auto Road-Trip" },
                                    { b: 0,  title: "Redwoods" },
                                    { b: 1,  title: "Heartland Farms" },
                                    { b: 2,  title: "Sonoran Canyon" },
                                    { b: 3,  title: "Maple Suburbs" },
                                    { b: 4,  title: "Downtown City" },
                                    { b: 5,  title: "Alpine Snow" }
                                ]
                                delegate: Button {
                                    width: units.gu(13.2)
                                    text: modelData.title
                                    color: road.lockedBiome === modelData.b ? "#2a9d8f" : "#232a3b"
                                    onClicked: road.lockedBiome = modelData.b
                                }
                            }
                        }

                        // 3. Time of Day & Weather
                        Label { text: "3. Time of Day & Weather"; font.bold: true; fontSize: "small"; color: "#8ce8ff" }
                        Row {
                            spacing: units.gu(0.8)
                            Button {
                                width: units.gu(11.5)
                                text: road.autoTimeCycle ? "Cycle: ON" : "Cycle: LOCK"
                                color: road.autoTimeCycle ? "#3a86ff" : "#232a3b"
                                onClicked: road.autoTimeCycle = !road.autoTimeCycle
                            }
                            Button { width: units.gu(9.5); text: "Dawn"; onClicked: { road.autoTimeCycle = false; road.timeOfDay = 6.2 } }
                            Button { width: units.gu(9.5); text: "Noon"; onClicked: { road.autoTimeCycle = false; road.timeOfDay = 12.5 } }
                            Button { width: units.gu(11.0); text: "Sunset"; color: "#b93e5e"; onClicked: { road.autoTimeCycle = false; road.timeOfDay = 17.6 } }
                            Button { width: units.gu(10.0); text: "Night"; color: "#1d2d50"; onClicked: { road.autoTimeCycle = false; road.timeOfDay = 23.5 } }
                        }

                        Row {
                            spacing: units.gu(0.8)
                            Repeater {
                                model: [
                                    { w: -1, title: "Auto Weather" },
                                    { w: 0,  title: "Clear" },
                                    { w: 1,  title: "Rainstorm" },
                                    { w: 2,  title: "Misty Fog" },
                                    { w: 3,  title: "Snowfall" }
                                ]
                                delegate: Button {
                                    width: units.gu(10.6)
                                    text: modelData.title
                                    color: road.weatherMode === modelData.w ? "#457b9d" : "#232a3b"
                                    onClicked: road.weatherMode = modelData.w
                                }
                            }
                        }

                        // 4. Driving Mode, Cruise Speed & Gyro/Tilt Steering Calibration
                        Label { text: "4. Steering, Zen Auto-Drive & Cabin Audio"; font.bold: true; fontSize: "small"; color: "#8ce8ff" }
                        Row {
                            spacing: units.gu(0.9)
                            Button {
                                width: units.gu(14.0)
                                text: road.autoDrive ? "Zen Cruise: ON" : "Zen Cruise: OFF"
                                color: road.autoDrive ? "#2ec4b6" : "#232a3b"
                                onClicked: road.autoDrive = !road.autoDrive
                            }
                            Button {
                                width: units.gu(13.5)
                                text: road.tiltSteerEnabled ? "Tilt Wheel: ON" : "Touch Steer"
                                color: road.tiltSteerEnabled ? "#3a86ff" : "#232a3b"
                                onClicked: road.tiltSteerEnabled = !road.tiltSteerEnabled
                            }
                            Button {
                                width: units.gu(13.5)
                                text: "Zero Tilt Now"
                                color: "#e07a2b"
                                onClicked: road.calibrateTiltZero()
                            }
                            Button {
                                width: units.gu(12.5)
                                text: road.invertTilt ? "Tilt: Inverted" : "Tilt: Normal"
                                color: "#232a3b"
                                onClicked: road.invertTilt = !road.invertTilt
                            }
                        }

                        Row {
                            width: parent.width
                            spacing: units.gu(1.2)
                            Label {
                                text: "Tilt Sens: " + road.steerSensitivity.toFixed(1) + "x"
                                width: units.gu(16)
                                anchors.verticalCenter: parent.verticalCenter
                                fontSize: "small"
                                color: "#ffffff"
                            }
                            Slider {
                                width: parent.width - units.gu(18)
                                minimumValue: 0.3
                                maximumValue: 2.0
                                live: true
                                Component.onCompleted: value = road.steerSensitivity
                                onValueChanged: {
                                    if (Math.abs(road.steerSensitivity - value) > 0.02) {
                                        road.steerSensitivity = value
                                    }
                                }
                            }
                        }

                        Row {
                            width: parent.width
                            spacing: units.gu(1.2)
                            Label {
                                text: "Cruise: " + Math.round(road.cruiseSpeedKmh) + " km/h"
                                width: units.gu(16)
                                anchors.verticalCenter: parent.verticalCenter
                                fontSize: "small"
                                color: "#ffffff"
                            }
                            Slider {
                                width: parent.width - units.gu(18)
                                minimumValue: 50
                                maximumValue: 190
                                live: true
                                Component.onCompleted: value = road.cruiseSpeedKmh
                                onValueChanged: {
                                    if (Math.abs(road.cruiseSpeedKmh - value) > 1.0) {
                                        road.cruiseSpeedKmh = value
                                    }
                                }
                            }
                        }

                        Row {
                            spacing: units.gu(1.0)
                            Button {
                                width: units.gu(17)
                                text: road.engineSound ? "Engine Purr: ON" : "Engine Purr: OFF"
                                color: road.engineSound ? "#5c4d7d" : "#232a3b"
                                onClicked: road.engineSound = !road.engineSound
                            }
                            Button {
                                width: units.gu(17)
                                text: road.headlights ? "Headlights: AUTO" : "Headlights: OFF"
                                color: road.headlights ? "#5c4d7d" : "#232a3b"
                                onClicked: road.headlights = !road.headlights
                            }
                            Button {
                                width: units.gu(18)
                                text: road.radioStationName
                                color: "#2b5876"
                                onClicked: road.cycleRadio()
                            }
                        }
                    }
                }
            }
        }
    }
}
