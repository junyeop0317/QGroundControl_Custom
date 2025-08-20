#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>

class VWorldSearch : public QObject {
    Q_OBJECT
    Q_PROPERTY(QList<QVariant> searchResults READ searchResults NOTIFY searchResultsChanged)

public:
    explicit VWorldSearch(QObject *parent = nullptr);

    QList<QVariant> searchResults() const { return _searchResults; }
    Q_INVOKABLE void search(const QString &query);
    Q_INVOKABLE void clearSearchResults();
    Q_INVOKABLE void selectResult(int index);

signals:
    void searchResultsChanged();
    void moveToCoordinate(double lat, double lon);   // QML에서 지도 이동용
    void addWaypoint(double lat, double lon);  // 웨이포인트 추가 시그널
private:
    void processSearchResponse(QNetworkReply *reply);
    void setSearchResults(const QList<QVariant> &results);

    QNetworkAccessManager *_networkManager;
    QList<QVariant> _searchResults;
};

