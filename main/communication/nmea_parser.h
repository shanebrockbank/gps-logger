/**
 * @file nmea_parser.h
 * @brief NMEA 0183 sentence parser for GPS data
 *
 * Parses NMEA sentences from GPS module and extracts position data.
 * Supports GPRMC (Recommended Minimum) and GPGGA (Fix Data) sentences.
 *
 * Example sentences:
 * $GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A
 * $GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47
 *
 * Thread Safety:
 * - All functions are reentrant (no global state)
 * - Caller responsible for thread safety of output structure
 */

#ifndef NMEA_PARSER_H
#define NMEA_PARSER_H

#include <stdint.h>
#include <stdbool.h>

/**
 * GPS fix quality indicators
 */
typedef enum {
    GPS_FIX_INVALID = 0,    // No fix
    GPS_FIX_GPS = 1,        // GPS fix (SPS)
    GPS_FIX_DGPS = 2,       // DGPS fix
    GPS_FIX_PPS = 3,        // PPS fix
    GPS_FIX_RTK = 4,        // Real Time Kinematic
    GPS_FIX_FLOAT_RTK = 5,  // Float RTK
    GPS_FIX_ESTIMATED = 6,  // Estimated (dead reckoning)
    GPS_FIX_MANUAL = 7,     // Manual input mode
    GPS_FIX_SIMULATION = 8  // Simulation mode
} gps_fix_quality_t;

/**
 * GPS position data structure
 */
typedef struct {
    // Position
    double latitude;        // Latitude in decimal degrees (-90 to 90)
    double longitude;       // Longitude in decimal degrees (-180 to 180)
    float altitude;         // Altitude above mean sea level in meters

    // Quality
    gps_fix_quality_t fix_quality;  // Fix quality indicator
    uint8_t satellites;     // Number of satellites in use
    float hdop;             // Horizontal dilution of precision

    // Motion
    float speed_knots;      // Speed over ground in knots
    float course;           // Course over ground in degrees

    // Time (UTC)
    uint8_t hour;           // Hour (0-23)
    uint8_t minute;         // Minute (0-59)
    uint8_t second;         // Second (0-59)

    // Date
    uint8_t day;            // Day of month (1-31)
    uint8_t month;          // Month (1-12)
    uint8_t year;           // Year (2-digit, add 2000)

    // Validity flags
    bool position_valid;    // True if lat/lon are valid
    bool time_valid;        // True if time is valid
    bool date_valid;        // True if date is valid
    bool altitude_valid;    // True if altitude is valid
} gps_data_t;

/**
 * @brief Parse an NMEA sentence and extract GPS data
 *
 * Automatically detects sentence type (GPRMC or GPGGA) and extracts
 * relevant fields. Updates only the fields present in the sentence type.
 *
 * @param sentence Null-terminated NMEA sentence (without \r\n)
 * @param data Pointer to GPS data structure to update
 *
 * @return true if sentence was valid and parsed successfully
 * @return false if sentence was invalid or checksum failed
 */
bool nmea_parse_sentence(const char *sentence, gps_data_t *data);

/**
 * @brief Validate NMEA sentence checksum
 *
 * NMEA checksum is XOR of all characters between $ and *
 *
 * @param sentence Null-terminated NMEA sentence
 *
 * @return true if checksum is valid
 * @return false if checksum is invalid or malformed
 */
bool nmea_validate_checksum(const char *sentence);

/**
 * @brief Convert GPS coordinates from NMEA format to decimal degrees
 *
 * NMEA format: DDMM.MMMM (degrees and minutes)
 * Decimal format: DD.DDDDDD (decimal degrees)
 *
 * Example: 4807.038 N -> 48.1173 degrees
 *
 * @param nmea_coord Coordinate in NMEA format (DDMM.MMMM or DDDMM.MMMM)
 * @param direction Direction character ('N', 'S', 'E', 'W')
 *
 * @return Coordinate in decimal degrees (negative for S/W)
 */
double nmea_coord_to_decimal(double nmea_coord, char direction);

#endif // NMEA_PARSER_H
