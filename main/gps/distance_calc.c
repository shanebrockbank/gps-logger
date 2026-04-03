#include "distance_calc.h"
#include <math.h>

#define EARTH_RADIUS_M  6371000.0

static const double DEG_TO_RAD = M_PI / 180.0;

float distance_calc_haversine(double lat1, double lon1, double lat2, double lon2)
{
    // Basic bounds check
    if (lat1 < -90.0 || lat1 > 90.0 || lat2 < -90.0 || lat2 > 90.0 ||
        lon1 < -180.0 || lon1 > 180.0 || lon2 < -180.0 || lon2 > 180.0) {
        return -1.0f;
    }

    double phi1 = lat1 * DEG_TO_RAD;
    double phi2 = lat2 * DEG_TO_RAD;
    double dphi = (lat2 - lat1) * DEG_TO_RAD;
    double dlam = (lon2 - lon1) * DEG_TO_RAD;

    double a = sin(dphi / 2.0) * sin(dphi / 2.0) +
               cos(phi1) * cos(phi2) *
               sin(dlam / 2.0) * sin(dlam / 2.0);

    double c = 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
    return (float)(EARTH_RADIUS_M * c);
}

float distance_calc_bearing(double lat1, double lon1, double lat2, double lon2)
{
    if (lat1 < -90.0 || lat1 > 90.0 || lat2 < -90.0 || lat2 > 90.0) {
        return -1.0f;
    }

    double phi1 = lat1 * DEG_TO_RAD;
    double phi2 = lat2 * DEG_TO_RAD;
    double dlam = (lon2 - lon1) * DEG_TO_RAD;

    double y = sin(dlam) * cos(phi2);
    double x = cos(phi1) * sin(phi2) - sin(phi1) * cos(phi2) * cos(dlam);
    double theta = atan2(y, x);

    float bearing = (float)(fmod(theta / DEG_TO_RAD + 360.0, 360.0));
    return bearing;
}
