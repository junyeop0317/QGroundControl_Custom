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
    
    // 환경 변수에서 API 키 가져오기 함수
    QString getVWorldApiKey() const;

    QNetworkAccessManager *_networkManager;
    QList<QVariant> _searchResults;
};

