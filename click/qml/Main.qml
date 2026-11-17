import QtQuick 2.12
import QtQuick.Window 2.12
import Lomiri.Components 1.3
import QtSensors 5.11
import HorizonRoads 1.0

Window {
    id: rootWin
    visible: true
    width: units.gu(45)
    height: units.gu(80)
    title: "Horizon Roads 3D"
    color: "#0b0f19"

    property bool forceLandscape: true
    property bool drawerOpen: false
    property bool minimalZenUi: false
    property bool sensorReady: false

    Timer {
        interval: 300
        running: true
        repeat: false
        onTriggered: rootWin.sensorReady = true
    }

    Connections {
        target: Qt.application
        onStateChanged: {
            if (Qt.application.state !== Qt.ApplicationActive) {
                road.paused = true
            } else if (!rootWin.drawerOpen) {
                road.paused = false
            }
        }
    }

    Loader {
        id: accelLoader
        active: rootWin.sensorReady && !road.paused && !road.autoDrive && road.tiltSteerEnabled
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

            // === WINDSHIELD FREE-LOOK & MANUAL PEDALS MULTI-TOUCH ===
            MultiPointTouchArea {
                id: driveTouch
                anchors.fill: parent
                enabled: !rootWin.drawerOpen
                minimumTouchPoints: 1
                maximumTouchPoints: 4

                property int lookId: -1
                property real prevX: 0
                property real prevY: 0
                property int steerDragId: -1
                property real steerOriginX: 0

                onPressed: {
                    for (var i = 0; i < touchPoints.length; ++i) {
                        var tp = touchPoints[i]
                        // Ignore top bar & bottom control dock
                        if (!rootWin.minimalZenUi && (tp.y < units.gu(5.6) || tp.y > stage.height - units.gu(6.2))) continue

                        // Manual mode fallback touch steering zone (left-lower quarter when Tilt is OFF)
                        if (!road.autoDrive && !road.tiltSteerEnabled &&
                            tp.x < stage.width * 0.42 && tp.y > stage.height * 0.52 && steerDragId === -1) {
                            steerDragId = tp.pointId
                            steerOriginX = tp.x
                            road.setTouchSteer(0)
                            continue
                        }

                        if (lookId === -1) {
                            lookId = tp.pointId
                            prevX = tp.x
                            prevY = tp.y
                        }
                    }
                }

                onUpdated: {
                    for (var i = 0; i < touchPoints.length; ++i) {
                        var tp = touchPoints[i]
                        if (tp.pointId === lookId) {
                            road.addViewDrag(tp.x - prevX, tp.y - prevY)
                            prevX = tp.x
                            prevY = tp.y
                        } else if (tp.pointId === steerDragId) {
                            var sVal = (tp.x - steerOriginX) / units.gu(8.0)
                            road.setTouchSteer(Math.max(-1.0, Math.min(1.0, sVal)))
                        }
                    }
                }

                function clearPts(pts) {
                    for (var i = 0; i < pts.length; ++i) {
                        var tp = pts[i]
                        if (tp.pointId === lookId) lookId = -1
                        if (tp.pointId === steerDragId) {
                            steerDragId = -1
                            road.setTouchSteer(0)
                        }
                    }
                }
                onReleased: clearPts(touchPoints)
                onCanceled: clearPts(touchPoints)
            }

            // Manual Mode Brake & Gas Pedals (visible only when Zen Auto-Drive is OFF!)
            Item {
                anchors.fill: parent
                visible: !road.autoDrive && !rootWin.drawerOpen && !rootWin.minimalZenUi

                // BRAKE Pedal (Left)
                Rectangle {
                    width: units.gu(9.5)
                    height: units.gu(6.6)
                    radius: units.gu(1.2)
                    anchors.left: parent.left
                    anchors.leftMargin: units.gu(2.0)
                    anchors.bottom: bottomDock.top
                    anchors.bottomMargin: units.gu(1.4)
                    color: brakeArea.pressed ? "#ccff5252" : "#66141824"
                    border.width: units.dp(1.5)
                    border.color: "#ff7b7b"

                    Label {
                        anchors.centerIn: parent
                        text: "BRAKE"
                        font.bold: true
                        fontSize: "small"
                        color: "#ffffff"
                    }
                    MouseArea {
                        id: brakeArea
                        anchors.fill: parent
                        onPressed: road.setBrakeHeld(true)
                        onReleased: road.setBrakeHeld(false)
                        onCanceled: road.setBrakeHeld(false)
                    }
                }

                // GAS / BOOST Pedal (Right)
                Rectangle {
                    width: units.gu(10.5)
                    height: units.gu(8.2)
                    radius: units.gu(1.2)
                    anchors.right: parent.right
                    anchors.rightMargin: units.gu(2.0)
                    anchors.bottom: bottomDock.top
                    anchors.bottomMargin: units.gu(1.4)
                    color: gasArea.pressed ? "#cc38e89c" : "#66141824"
                    border.width: units.dp(1.5)
                    border.color: "#5effb8"

                    Label {
                        anchors.centerIn: parent
                        text: "GAS"
                        font.bold: true
                        fontSize: "medium"
                        color: "#ffffff"
                    }
                    MouseArea {
                        id: gasArea
                        anchors.fill: parent
                        onPressed: road.setGasHeld(true)
                        onReleased: road.setGasHeld(false)
                        onCanceled: road.setGasHeld(false)
                    }
                }

                // Subtle hint pill for Tilt vs Touch steering
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: bottomDock.top
                    anchors.bottomMargin: units.gu(1.0)
                    width: tiltHint.implicitWidth + units.gu(2.4)
                    height: units.gu(3.2)
                    radius: units.gu(1.6)
                    color: "#880e1320"

                    Label {
                        id: tiltHint
                        anchors.centerIn: parent
                        text: road.tiltSteerEnabled
                              ? "Tilt phone to steer · Tap here to Recenter Wheel"
                              : "Drag bottom-left to steer · Drag sky to look around"
                        fontSize: "x-small"
                        color: "#d4e2f4"
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: if (road.tiltSteerEnabled) road.calibrateTiltZero()
                    }
                }
            }

            // === HIGHWAY STATE / BIOME WELCOME SIGN BANNER ===
            Rectangle {
                visible: road.bannerText.length > 0 && !rootWin.drawerOpen
                anchors.horizontalCenter: parent.horizontalCenter
                y: units.gu(6.6)
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
                    anchors.leftMargin: units.gu(1.4)
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

            // === BOTTOM COCKPIT QUICK SWITCH BAR ===
            Rectangle {
                id: bottomDock
                visible: !rootWin.minimalZenUi && !rootWin.drawerOpen
                anchors.bottom: parent.bottom
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottomMargin: units.gu(0.8)
                width: dockRow.implicitWidth + units.gu(2.0)
                height: units.gu(4.6)
                radius: units.gu(2.3)
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
