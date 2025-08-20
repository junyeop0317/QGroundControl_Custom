#include "VWorldSearch.h"
#include <QNetworkRequest>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

VWorldSearch::VWorldSearch(QObject *parent)
    : QObject(parent), _networkManager(new QNetworkAccessManager(this)) {
}

void VWorldSearch::setSearchResults(const QList<QVariant> &results) {
    if (_searchResults != results) {
        _searchResults = results;
        emit searchResultsChanged();
    }
}

void VWorldSearch::clearSearchResults() {
    if (!_searchResults.isEmpty()) {
        _searchResults.clear();
        emit searchResultsChanged();
    }
}

void VWorldSearch::search(const QString &query) {
    
    _searchResults.clear();
    emit searchResultsChanged();
    
    
    qDebug() << "Searching for:" << query;
    QUrl url("http://api.vworld.kr/req/search");
    QUrlQuery urlQuery;
    urlQuery.addQueryItem("service", "search");
    urlQuery.addQueryItem("request", "search");
    urlQuery.addQueryItem("version", "2.0");
    urlQuery.addQueryItem("size", "10");   // 최대 10개 검색
    urlQuery.addQueryItem("page", "1");
    urlQuery.addQueryItem("type", "place"); // 장소 검색
    urlQuery.addQueryItem("query", query);
    urlQuery.addQueryItem("format", "json");
    urlQuery.addQueryItem("key", "C976EFA0-5CEE-32C0-8A42-699FE3E12546"); // << API KEY
    url.setQuery(urlQuery);

    qDebug() << "Request URL:" << url.toString();

    QNetworkRequest request(url);
    QNetworkReply *reply = _networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        processSearchResponse(reply);
        reply->deleteLater();
    });
}

void VWorldSearch::processSearchResponse(QNetworkReply *reply) {
    _searchResults.clear();
    if (reply->error() == QNetworkReply::NoError) {
        QByteArray data = reply->readAll();
        qDebug() << "API Response:" << data;
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (!doc.isNull()) {
            QJsonObject response = doc.object().value("response").toObject();
            QJsonObject resultObj = response.value("result").toObject();
            QJsonArray items = resultObj.value("items").toArray();

            qDebug() << "Found" << items.size() << "results";

            for (const QJsonValue &item : items) {
                QJsonObject obj = item.toObject();
                QJsonObject point = obj.value("point").toObject();

                QVariantMap result;
                result["title"] = obj.value("title").toString();
                result["lat"] = point.value("y").toString().toDouble();
                result["lon"] = point.value("x").toString().toDouble();

                qDebug() << "Parsed:" << result["title"].toString()
                         << result["lat"].toDouble()
                         << result["lon"].toDouble();

                _searchResults.append(result);
            }
        } else {
            qDebug() << "Invalid JSON response";
        }
        emit searchResultsChanged();
    } else {
        qDebug() << "V-World API error:" << reply->errorString();
    }
}

void VWorldSearch::selectResult(int index) {
    if (index >= 0 && index < _searchResults.size()) {
        QVariantMap result = _searchResults[index].toMap();
        double lat = result["lat"].toDouble();
        double lon = result["lon"].toDouble();

        emit moveToCoordinate(lat, lon);   // 지도 이동
        emit addWaypoint(lat, lon);        // 웨이포인트 추가
    }
}

