import QtQuick 2.15
import QtQuick.Controls 2.15
import QtPositioning 5.15
import QGroundControl.VWorldSearch 1.0
import QGroundControl.Controls 1.0

Column {
    id: searchPanel
    width: 300
    spacing: 5
    visible: false

    property var editorMap
    property var missionController
    signal requestAddWaypoint(real lat, real lon)  // 시그널로 PlanView에 전달

    VWorldSearch {
        id: vworldSearch
        onMoveToCoordinate: {
            if (editorMap) {
                editorMap.center = QtPositioning.coordinate(lat, lon)
                editorMap.zoomLevel = 18
                requestAddWaypoint(lat, lon)  // 시그널 호출
                searchPanel.visible = false
            }
        }
    }

    QGCTextField {
        id: searchField
        width: parent.width
        placeholderText: qsTr("장소 검색 : ")

        onTextChanged: {
            if (text.length > 0) vworldSearch.search(text)
            else vworldSearch.clearSearchResults()
        }

        onAccepted: {
            if (vworldSearch.searchResults.length > 0) vworldSearch.selectResult(0)
        }
    }

    ListView {
        id: searchResultsView
        width: parent.width
        height: Math.min(contentHeight, 200)
        visible: vworldSearch.searchResults.length > 0
        model: vworldSearch.searchResults

        delegate: Rectangle {
            width: searchResultsView.width
            height: 30
            color: mouseArea.containsMouse ? "#d3d3d3" : "white"

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: modelData.title
                leftPadding: 5
            }

            MouseArea {
                id: mouseArea
                anchors.fill: parent
                hoverEnabled: true

                onClicked: {
                    vworldSearch.selectResult(index)
                    searchField.text = modelData.title
                    searchPanel.visible = false
                }
            }
        }

        ScrollBar.vertical: ScrollBar {}
    }

    function addWaypoint(lat, lon) {
        if (!missionController) {
            console.log("MissionController not ready")
            return
        }

        var coordinate = QtPositioning.coordinate(lat, lon)

        // PlanView 초기화 지연 + 첫 검색 확인
        function tryInsert() {
            if (!missionController.visualItems) {
                Qt.callLater(tryInsert)
                return
            }


        }

        Qt.callLater(tryInsert)
    }
}
