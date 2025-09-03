#include "WeatherManager.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QXmlStreamReader>
#include <QDebug>
#include <QDateTime>
#include <QTimer>

// ------------------- 생성자: QNetworkAccessManager 초기화 -------------------
WeatherManager::WeatherManager(QObject *parent)
    : QObject(parent), xGrid(0), yGrid(0)
{
    manager = new QNetworkAccessManager(this);
    qDebug() << "WeatherManager initialized";
}

// ------------------- 위도/경도를 받아 날씨 데이터 요청 시작 -------------------
void WeatherManager::fetchWeatherData(double lat, double lon)
{
    qDebug() << "WeatherManager: fetchWeatherData called with latitude =" << lat << ", longitude =" << lon;
    if (lat < -90 || lat > 90 || lon < -180 || lon > 180) {
        qDebug() << "Invalid coordinates: latitude =" << lat << ", longitude =" << lon;
        emit weatherDataReady(QVariantList() << QVariantMap({{"error", "Invalid latitude or longitude"}}));
        return;
    }
    convertLatLonToGrid(lat, lon);
}

// ------------------- 위도/경도를 기상청 격자 좌표(X,Y)로 변환 -------------------
void WeatherManager::convertLatLonToGrid(double lat, double lon)
{
    QString apiKey = QString::fromLocal8Bit(qgetenv("weather_key"));
    if (apiKey.isEmpty()) {
        qWarning() << "환경 변수 weather_key가 설정되지 않았습니다!";
        emit weatherDataReady(QVariantList() << QVariantMap({{"error", "Missing weather API key"}}));
        return;
    }
    QString urlStr = QString(
        "https://apihub.kma.go.kr/api/typ01/cgi-bin/url/nph-dfs_xy_lonlat?"
        "lon=%1&lat=%2&help=0&authKey=%3")
        .arg(QString::number(lon, 'f', 6))
        .arg(QString::number(lat, 'f', 6))
        .arg(apiKey);

    QUrl url(urlStr);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0");
    request.setRawHeader("Accept", "text/plain");

    qDebug() << "Requesting grid conversion: URL =" << urlStr;
    QNetworkReply* reply = manager->get(request);
    connect(reply, &QNetworkReply::finished, [this, reply, lat, lon]() {
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray responseData = reply->readAll();
            qDebug() << "Grid conversion response:" << responseData;
            QStringList lines = QString(responseData).split("\n", Qt::SkipEmptyParts);
            for (const QString& line : lines) {
                if (line.contains(",") && !line.startsWith("#")) {
                    QStringList values = line.split(",", Qt::SkipEmptyParts);
                    if (values.size() >= 4) {
                        bool xOk, yOk;
                        xGrid = values[2].trimmed().toInt(&xOk);
                        yGrid = values[3].trimmed().toInt(&yOk);
                        if (xOk && yOk && xGrid > 0 && yGrid > 0) {
                            qDebug() << "Grid conversion successful: x =" << xGrid << ", y =" << yGrid;
                            requestWeatherData(xGrid, yGrid);
                            break;
                        }
                    }
                }
            }
            if (xGrid == 0 || yGrid == 0) {
                qDebug() << "Grid conversion failed for lat =" << lat << ", lon =" << lon << ": No valid grid coordinates found";
                emit weatherDataReady(QVariantList() << QVariantMap({{"error", "Failed to convert coordinates to grid"}}));
            }
        } else {
            qDebug() << "Grid conversion API error for lat =" << lat << ", lon =" << lon << ":" << reply->errorString();
            emit weatherDataReady(QVariantList() << QVariantMap({{"error", reply->errorString()}}));
        }
        reply->deleteLater();
    });
}

// ------------------- 격자 좌표를 이용해 실제 날씨 API 요청 및 XML 파싱 -------------------
void WeatherManager::requestWeatherData(int gridX, int gridY)
{
    QString apiKey = QString::fromLocal8Bit(qgetenv("weather_key"));
    if (apiKey.isEmpty()) {
        qWarning() << "환경 변수 weather_key가 설정되지 않았습니다!";
        emit weatherDataReady(QVariantList() << QVariantMap({{"error", "Missing weather API key"}}));
        return;
    }
    QDateTime current = QDateTime::currentDateTime();

    int hour = current.time().hour();
    int minute = current.time().minute();
    int baseMinute;

    if (minute < 45) {
        if (hour == 0) hour = 23;
        else hour -= 1;
        baseMinute = 45;
    } else {
        baseMinute = 45;
    }

    int baseTime = hour * 100 + baseMinute;
    QString baseDate = current.toString("yyyyMMdd");

    qDebug() << "Requesting weather data: base_date =" << baseDate << ", base_time =" << baseTime << ", nx =" << gridX << ", ny =" << yGrid;

    QString urlStr = QString(
        "https://apihub.kma.go.kr/api/typ02/openApi/VilageFcstInfoService_2.0/getUltraSrtFcst?"
        "pageNo=1&numOfRows=1000&dataType=XML&base_date=%1&base_time=%2&nx=%3&ny=%4&authKey=%5")
        .arg(baseDate)
        .arg(baseTime, 4, 10, QChar('0'))
        .arg(gridX)
        .arg(gridY)
        .arg(apiKey);

    QUrl url(urlStr);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0");
    request.setRawHeader("Accept", "application/xml");

    qDebug() << "Weather API request URL:" << urlStr;

    QNetworkReply* reply = manager->get(request);
    connect(reply, &QNetworkReply::finished, [this, reply, gridX, gridY]() {
        if (reply->error() != QNetworkReply::NoError) {
            qDebug() << "Weather API error: nx =" << gridX << ", ny =" << gridY << ", error =" << reply->errorString();
            emit weatherDataReady(QVariantList() << QVariantMap({{"error", reply->errorString()}}));
            reply->deleteLater();
            return;
        }

        QByteArray responseData = reply->readAll();
        qDebug() << "Weather API response:" << responseData;
        QXmlStreamReader xml(responseData);

        QVariantList weatherList;
        QString currentCategory, currentFcstTime;
        QString resultCode, resultMsg;
        QMap<QString, QVariantMap> timeData;

        while (!xml.atEnd() && !xml.hasError()) {
            xml.readNext();
            if (xml.isStartElement()) {
                if (xml.name() == "resultCode") resultCode = xml.readElementText();
                else if (xml.name() == "resultMsg") resultMsg = xml.readElementText();
                else if (xml.name() == "item") { currentCategory.clear(); currentFcstTime.clear(); }
                else if (xml.name() == "category") currentCategory = xml.readElementText();
                else if (xml.name() == "fcstTime") currentFcstTime = xml.readElementText();
                else if (xml.name() == "fcstValue" && !currentCategory.isEmpty() && !currentFcstTime.isEmpty()) {
                    QString value = xml.readElementText();
                    if (currentCategory == "T1H" || currentCategory == "PTY" ||
                        currentCategory == "WSD" || currentCategory == "VEC" ||
                        currentCategory == "RN1" || currentCategory == "SKY" ||
                        currentCategory == "REH") {
                        timeData[currentFcstTime][currentCategory] = value;
                    }
                }
            }
        }

        if (xml.hasError()) {
            qDebug() << "XML parsing error: nx =" << gridX << ", ny =" << gridY << ", error =" << xml.errorString();
            emit weatherDataReady(QVariantList() << QVariantMap({{"error", "XML parsing error: " + xml.errorString()}}));
        } else if (resultCode != "00") {
            qDebug() << "API error: nx =" << gridX << ", ny =" << gridY << ", resultCode =" << resultCode << ", resultMsg =" << resultMsg;
            emit weatherDataReady(QVariantList() << QVariantMap({{"error", "API error: " + resultMsg}}));
        } else if (!timeData.isEmpty()) {
            QStringList times = timeData.keys();
            times.sort();
            for (const QString& fcstTime : times) {
                QVariantMap data = timeData[fcstTime];
                data["time"] = fcstTime;
                weatherList.append(data);
            }
            qDebug() << "Weather data parsed successfully: nx =" << gridX << ", ny =" << gridY << ", data =" << weatherList;
            emit weatherDataReady(weatherList);
        } else {
            qDebug() << "Weather data parsing failed: nx =" << gridX << ", ny =" << gridY << ": No valid data found";
            emit weatherDataReady(QVariantList() << QVariantMap({{"error", "No valid weather data found"}}));
        }

        reply->deleteLater();
    });
}
