#ifndef WEATHERMANAGER_H
#define WEATHERMANAGER_H

#include <QObject>
#include <QDebug>
#include <QNetworkAccessManager>
#include <QVariantList> // QVariantList 사용 (QML에서 ListModel과 호환 가능)

// ------------------- WeatherManager 클래스 -------------------
// 날씨 데이터를 요청하고 QML로 전달해주는 클래스
class WeatherManager : public QObject
{
    Q_OBJECT // Qt의 신호/슬롯, 프로퍼티, 메타 객체 기능 사용 가능하게 함

public:
    // ------------------- 생성자 -------------------
    explicit WeatherManager(QObject *parent = nullptr); 
    // 객체 초기화, 부모 QObject 설정 가능 (메모리 관리 용)

public slots:
    // ------------------- 날씨 데이터 요청 슬롯 -------------------
    void fetchWeatherData(double lat, double lon); 
    // QML에서 호출 가능, 위도(lat)/경도(lon) 기반으로 날씨 API 요청 시작

signals:
    // ------------------- 날씨 데이터 준비 완료 시 발생하는 시그널 -------------------
    void weatherDataReady(const QVariantList &weatherData); 
    // 날씨 데이터를 QML로 전달
    // QVariantMap이 여러 개 들어있는 QVariantList 형태로 전달됨

private:
    // ------------------- 좌표 변환 함수 -------------------
    void convertLatLonToGrid(double lat, double lon); 
    // 위도/경도를 기상청 API용 격자 좌표(xGrid, yGrid)로 변환

    // ------------------- 날씨 API 요청 함수 -------------------
    void requestWeatherData(int gridX, int gridY); 
    // 변환된 격자 좌표를 이용해 실제 날씨 데이터 요청

    QNetworkAccessManager *manager; // HTTP 요청을 보내고 응답을 받는 매니저
    int xGrid; // 변환된 X 격자 좌표
    int yGrid; // 변환된 Y 격자 좌표
};

#endif // WEATHERMANAGER_H

