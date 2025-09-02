import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Weather 1.0
import QGroundControl

Item {
    id: weatherPopup
    width: 800
    height: 250
    visible: false
    z: 1000
    opacity: 0.7 // 전체 팝업의 투명도를 50%로 설정
    property var editorMap
    property var weatherManager
    property var planView
    property string weatherMode: "none"   // "none" | "mouse" | "launch" | "takeoff" | "drone" | "update"


    // 투명 배경 및 외곽선 제거 (opacity는 Item에 적용하므로 여기서는 기본 설정 유지)
    Rectangle {
        anchors.fill: parent
        radius: 12
        border.width: 0 // 흰색 외곽선 제거
        color: "transparent"
    }

    Column {
        id: weatherToolbar
        width: 130
        spacing: 5
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.leftMargin: 50 // 오른쪽으로 50px 이동 (Cadastral 겹침 방지)
        anchors.topMargin: -2 // Launch 버튼 윗선과 시간 칸 윗선 맞춤을 위해 조정

        Button {
            id: launchBtn
            text: qsTr("Launch")
            enabled: true
            onClicked: {
                console.log("Launch button clicked");
                if (!planView) {
                    console.log("Error: planView is not defined");
                    mainWindow.showMessageDialog(qsTr("Error"), qsTr("planView is not defined"));
                    return;
                }
                var coordinate = planView.getLaunchCoordinate();
                if (coordinate.isValid) {
                    console.log("Requesting weather for Launch: latitude =", coordinate.latitude, "longitude =", coordinate.longitude);
                    if (weatherManager) {
                        weatherManager.fetchWeatherData(coordinate.latitude, coordinate.longitude);
                        mouseEnabled = false;
                    } else {
                        console.log("Error: weatherManager is not defined");
                        mainWindow.showMessageDialog(qsTr("Error"), qsTr("weatherManager is not defined"));
                    }
                } else {
                    console.log("No valid Launch coordinate found");
                    mainWindow.showMessageDialog(qsTr("Error"), qsTr("No valid Launch coordinate found."));
                }
            }
        }
        Button {
            id: takeOffBtn
            text: qsTr("TakeOff")
            enabled: true
            onClicked: {
                console.log("TakeOff button clicked");
                if (!planView) {
                    console.log("Error: planView is not defined");
                    mainWindow.showMessageDialog(qsTr("Error"), qsTr("planView is not defined"));
                    return;
                }
                var coordinate = planView.getTakeoffCoordinate();
                if (coordinate.isValid) {
                    console.log("Requesting weather for TakeOff: latitude =", coordinate.latitude, "longitude =", coordinate.longitude);
                    if (weatherManager) {
                        weatherManager.fetchWeatherData(coordinate.latitude, coordinate.longitude);
                        mouseEnabled = false;
                    } else {
                        console.log("Error: weatherManager is not defined");
                        mainWindow.showMessageDialog(qsTr("Error"), qsTr("weatherManager is not defined"));
                    }
                } else {
                    console.log("No valid TakeOff coordinate found");
                    mainWindow.showMessageDialog(qsTr("Error"), qsTr("No valid TakeOff coordinate found."));
                }
            }
        }

        Button {
            id: droneBtn
            text: qsTr("Drone")
            onClicked: {
                console.log("Drone button clicked")

                var activeVehicle = QGroundControl.multiVehicleManager.activeVehicle
                if (activeVehicle && activeVehicle.coordinate.isValid) {
                    var lat = activeVehicle.coordinate.latitude
                    var lon = activeVehicle.coordinate.longitude

                    // ✅ 버튼 누를 때만 로그
                    console.log("Drone current position: latitude =", lat, "longitude =", lon)

                    if (weatherManager) {
                        weatherManager.fetchWeatherData(lat, lon)
                        mouseBtn.checked = false
                    } else {
                        console.log("Error: weatherManager is not defined")
                        mainWindow.showMessageDialog(qsTr("Error"), qsTr("weatherManager is not defined"))
                    }
                } else {
                    console.log("No valid Drone coordinate found")
                    mainWindow.showMessageDialog(qsTr("Error"), qsTr("No valid Drone coordinate found."))
                }
            }
        }
        Button {
            id: mouseBtn
            text: qsTr("Mouse")
            checkable: true
            checked: false
            onClicked: {
                console.log("Mouse button clicked, checked =", checked);
                weatherMode = checked ? "mouse" : "none";
            }
        }

        Button {
            id: updateBtn
            text: qsTr("Update")
            onClicked: {
                console.log("Update button clicked");
                weatherMode = "update";
                if (editorMap && weatherManager) {
                    var center = editorMap.center;
                    console.log("Requesting weather for map center:", center.latitude, center.longitude);
                    weatherManager.fetchWeatherData(center.latitude, center.longitude);
                } else {
                    console.log("Error: editorMap or weatherManager is not defined");
                    mainWindow.showMessageDialog(qsTr("Error"), qsTr("editorMap or weatherManager is not defined"));
                }
            }
        }
    }

    Flickable {                      // 날씨 정보 스크롤 영역
        id: weatherFlick
        anchors.top: parent.top
        anchors.left: weatherToolbar.right
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        contentWidth: weatherRow.width
        clip: true
        Row {
            id: weatherRow
            spacing: 2
            Column {
                spacing: 1
                width: 80
                Repeater {                                // 날씨 데이터 칸
                    model: ["시간", "기온", "하늘", "강수량", "습도", "풍속", "풍향"]
                    Rectangle {
                        width: 80
                        height: 30
                        color: "#e0e0e0" // 연한 회색으로 통일
                        border.color: "#888"
                        border.width: 1
                        Text {
                            anchors.centerIn: parent
                            text: modelData
                            font.pixelSize: 12
                            font.bold: true
                        }
                    }
                }
            }
            Repeater {
                model: weatherModel
                delegate: Column {
                    spacing: 1
                    width: 80
                    function getSkyIcon(sky) {
                        switch (sky) {
                            case "1": return "☀️"; // 맑음
                            case "3": return "☁️"; // 구름 많음
                            case "4": return "🌥️"; // 흐림
                            default: return "-";
                        }
                    }
                    function getWindDirectionArrow(vec) {
                        var deg = parseInt(vec) || 0;
                        if (deg >= 337.5 || deg < 22.5) return "↑"; // 북
                        else if (deg >= 22.5 && deg < 67.5) return "↗"; // 북동
                        else if (deg >= 67.5 && deg < 112.5) return "→"; // 동
                        else if (deg >= 112.5 && deg < 157.5) return "↘"; // 남동
                        else if (deg >= 157.5 && deg < 202.5) return "↓"; // 남
                        else if (deg >= 202.5 && deg < 247.5) return "↙"; // 남서
                        else if (deg >= 247.5 && deg < 292.5) return "←"; // 서
                        else if (deg >= 292.5 && deg < 337.5) return "↖"; // 북서
                        return "↗";
                    }
                    Rectangle {
                        width: 80
                        height: 30
                        color: "#e0e0e0" // 시간: 연한 회색
                        border.color: "#ccc"
                        border.width: 1
                        Text {
                            anchors.centerIn: parent
                            text: model.time.slice(0, 2) + ":" + model.time.slice(2) // 1300 -> 13:00
                            font.pixelSize: 12
                        }
                    }
                    Rectangle {
                        width: 80
                        height: 30
                        color: "#f0f0f0" // 기온: 더 연한 회색
                        border.color: "#ccc"
                        border.width: 1
                        Text {
                            anchors.centerIn: parent
                            text: model.T1H + "℃"
                            font.pixelSize: 12
                            font.bold: true
                        }
                    }
                    Rectangle {
                        width: 80
                        height: 30
                        color: "#f0f0f0" // 하늘: 더 연한 회색
                        border.color: "#ccc"
                        border.width: 1
                        Text {
                            anchors.centerIn: parent
                            text: getSkyIcon(model.SKY)
                            font.pixelSize: 14
                        }
                    }
                    Rectangle {
                        width: 80
                        height: 30
                        color: "#f0f0f0" // 강수량: 더 연한 회색
                        border.color: "#ccc"
                        border.width: 1
                        Text {
                            anchors.centerIn: parent
                            text: model.RN1 || "0mm"
                            font.pixelSize: 12
                        }
                    }
                    Rectangle {
                        width: 80
                        height: 30
                        color: "#f0f0f0" // 습도: 더 연한 회색
                        border.color: "#ccc"
                        border.width: 1
                        Text {
                            anchors.centerIn: parent
                            text: model.REH + "%"
                            font.pixelSize: 12
                        }
                    }
                    Rectangle {
                        width: 80
                        height: 30
                        color: "#f0f0f0" // 풍속: 더 연한 회색
                        border.color: "#ccc"
                        border.width: 1
                        Text {
                            anchors.centerIn: parent
                            text: model.WSD + "m/s"
                            font.pixelSize: 12
                        }
                    }
                    Rectangle {
                        width: 80
                        height: 30
                        color: "#f0f0f0" // 풍향: 더 연한 회색
                        border.color: "#ccc"
                        border.width: 1
                        Text {
                            anchors.centerIn: parent
                            text: getWindDirectionArrow(model.VEC) + " (" + model.VEC + "°)"
                            font.pixelSize: 12
                        }
                    }
                }
            }
        }
    }

    // 날씨 데이터를 저장하는 모델
    ListModel { id: weatherModel }

    // weatherManager에서 받은 데이터를 UI로 표시
    function updateWeatherUI(data) {
        console.log("updateWeatherUI called:", JSON.stringify(data));
        weatherModel.clear();
        if (!data || data.length === 0 || (data.length > 0 && data[0].error)) {
            var errorMsg = data && data.length > 0 ? data[0].error : "No weather data available";
            console.log("Weather error:", errorMsg);
            weatherModel.append({
                time: "N/A", T1H: "N/A", RN1: "0mm", SKY: "1", REH: "N/A", WSD: "N/A", VEC: "N/A"
            });
            mainWindow.showMessageDialog(qsTr("Error"), qsTr(errorMsg));
            return;
        }
        for (var i = 0; i < data.length && i < 6; i++) {
            var item = data[i];
            weatherModel.append({
                time: item.time || "N/A",
                T1H: item.T1H || "N/A",
                RN1: item.RN1 === "강수없음" ? "0mm" : item.RN1 || "0mm",
                SKY: item.SKY || "1",
                REH: item.REH || "N/A",
                WSD: item.WSD || "N/A",
                VEC: item.VEC || "N/A"
            });
        }
    }

    // weatherManager signal 연결
    Connections {
        target: weatherManager
        function onWeatherDataReady(data) {
            console.log("WeatherPopup received data:", JSON.stringify(data));
            updateWeatherUI(data);
        }
    }

    // 초기화 완료 로그
    Component.onCompleted: {
        console.log("WeatherPopup initialized, weatherManager:", weatherManager ? "Valid" : "Null",
                    "planView:", planView ? "Valid" : "Null");
    }

    // 팝업 표시/숨김 시 로그
    onVisibleChanged: {
        console.log("WeatherPopup visibility changed, visible:", visible, "weatherMode:", weatherMode);
    }
}
