#ifndef DISTANCE_CALC_H
#define DISTANCE_CALC_H

/**
 * @brief Haversine great-circle distance between two GPS coordinates.
 * @return Distance in metres, or -1.0f if inputs are invalid.
 */
float distance_calc_haversine(double lat1_deg, double lon1_deg,
                              double lat2_deg, double lon2_deg);

/**
 * @brief Bearing from point 1 to point 2.
 * @return Bearing in degrees (0–360, 0 = North), or -1.0f if invalid.
 */
float distance_calc_bearing(double lat1_deg, double lon1_deg,
                            double lat2_deg, double lon2_deg);

#endif // DISTANCE_CALC_H
