/****************************************************************************
 *
 * VWorld TMS Map Provider for QGroundControl
 *
 ****************************************************************************/

#pragma once

#include "MapProvider.h"
#include <QtLocation/private/qgeomaptype_p.h>
#include <QtCore/QString>
#include <QtCore/QVariantMap>

class VWorldTMSMapProvider : public MapProvider
{
public:
    VWorldTMSMapProvider();

protected:
    QString _getURL(int x, int y, int zoom) const override;
};

