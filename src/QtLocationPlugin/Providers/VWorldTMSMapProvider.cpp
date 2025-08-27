/****************************************************************************
 *
 * VWorld TMS Map Provider for QGroundControl
 *
 ****************************************************************************/

#include "VWorldTMSMapProvider.h"
#include <QtCore/QProcessEnvironment>

VWorldTMSMapProvider::VWorldTMSMapProvider()
    : MapProvider(
        QStringLiteral("VWorldTMS"),
        QStringLiteral(""),             // referrer
        QStringLiteral("png"),          // 기본 이미지 포맷
        13652,                          // 평균 타일 크기
        QGeoMapType::CustomMap)         // MapStyle
{
    // _mapTypes는 QGC 내부 MapProvider에서 관리
    // 필요시 별도 코드로 Map Type 등록 가능
}

QString VWorldTMSMapProvider::_getURL(int x, int y, int zoom) const
{
    QString key = QString::fromUtf8(qgetenv("v_world_key"));
    // TMS Y 좌표 변환
    int tmsY = (1 << zoom) - 1 - y;

    // VWorld TMS 요청 URL
    return QStringLiteral("https://api.vworld.kr/req/tms/1.0.0/%1/Hybrid/%2/%3/%4.png")
        .arg(key)
        .arg(zoom)
        .arg(tmsY)
        .arg(x);
}

