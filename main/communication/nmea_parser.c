/**
 * @file nmea_parser.c
 * @brief NMEA sentence parser implementation
 */

#include "nmea_parser.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

static const char *TAG = "NMEA";

// NMEA sentence prefixes
#define GPRMC_PREFIX "$GPRMC"
#define GPGGA_PREFIX "$GPGGA"
#define GNRMC_PREFIX "$GNRMC"  // Modern GPS may use GN instead of GP
#define GNGGA_PREFIX "$GNGGA"

/**
 * @brief Extract field from comma-separated NMEA sentence
 *
 * @param sentence NMEA sentence
 * @param field_num Field number (0-indexed)
 * @param buffer Output buffer for field
 * @param buffer_size Size of output buffer
 *
 * @return true if field extracted successfully
 */
static bool nmea_get_field(const char *sentence, uint8_t field_num, char *buffer, size_t buffer_size)
{
    if (sentence == NULL || buffer == NULL || buffer_size == 0) {
        return false;
    }

    const char *start = sentence;
    uint8_t current_field = 0;

    // Skip to the desired field
    while (current_field < field_num && *start != '\0') {
        if (*start == ',') {
            current_field++;
        }
        start++;
    }

    if (current_field != field_num) {
        buffer[0] = '\0';
        return false;
    }

    // Extract field until next comma or end
    size_t i = 0;
    while (*start != ',' && *start != '*' && *start != '\0' && i < buffer_size - 1) {
        buffer[i++] = *start++;
    }
    buffer[i] = '\0';

    return true;
}

/**
 * @brief Parse time from NMEA HHMMSS.SS format
 */
static bool nmea_parse_time(const char *time_str, gps_data_t *data)
{
    if (strlen(time_str) < 6) {
        return false;
    }

    char buf[3] = {0};

    // Hours
    buf[0] = time_str[0];
    buf[1] = time_str[1];
    data->hour = atoi(buf);

    // Minutes
    buf[0] = time_str[2];
    buf[1] = time_str[3];
    data->minute = atoi(buf);

    // Seconds
    buf[0] = time_str[4];
    buf[1] = time_str[5];
    data->second = atoi(buf);

    data->time_valid = true;
    return true;
}

/**
 * @brief Parse date from NMEA DDMMYY format
 */
static bool nmea_parse_date(const char *date_str, gps_data_t *data)
{
    if (strlen(date_str) < 6) {
        return false;
    }

    char buf[3] = {0};

    // Day
    buf[0] = date_str[0];
    buf[1] = date_str[1];
    data->day = atoi(buf);

    // Month
    buf[0] = date_str[2];
    buf[1] = date_str[3];
    data->month = atoi(buf);

    // Year
    buf[0] = date_str[4];
    buf[1] = date_str[5];
    data->year = atoi(buf);

    data->date_valid = true;
    return true;
}

double nmea_coord_to_decimal(double nmea_coord, char direction)
{
    // Extract degrees (integer part before last 2 digits of minutes)
    int degrees = (int)(nmea_coord / 100.0);

    // Extract minutes (remainder)
    double minutes = nmea_coord - (degrees * 100.0);

    // Convert to decimal degrees
    double decimal = degrees + (minutes / 60.0);

    // Apply direction (S and W are negative)
    if (direction == 'S' || direction == 'W') {
        decimal = -decimal;
    }

    return decimal;
}

bool nmea_validate_checksum(const char *sentence)
{
    if (sentence == NULL || sentence[0] != '$') {
        return false;
    }

    // Find the asterisk that precedes the checksum
    const char *asterisk = strchr(sentence, '*');
    if (asterisk == NULL) {
        ESP_LOGW(TAG, "No checksum found in sentence");
        return false;
    }

    // Calculate checksum (XOR of all chars between $ and *)
    uint8_t calculated = 0;
    for (const char *p = sentence + 1; p < asterisk; p++) {
        calculated ^= *p;
    }

    // Parse the provided checksum (2 hex digits after *)
    uint8_t provided = 0;
    if (sscanf(asterisk + 1, "%2hhx", &provided) != 1) {
        ESP_LOGW(TAG, "Invalid checksum format");
        return false;
    }

    if (calculated != provided) {
        ESP_LOGW(TAG, "Checksum mismatch: calculated=0x%02X, provided=0x%02X",
                 calculated, provided);
        return false;
    }

    return true;
}

/**
 * @brief Parse GPRMC sentence
 *
 * Format: $GPRMC,time,status,lat,N/S,lon,E/W,speed,course,date,mag_var,var_dir*checksum
 * Example: $GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A
 */
static bool nmea_parse_gprmc(const char *sentence, gps_data_t *data)
{
    char field[20];

    // Field 1: Time
    if (nmea_get_field(sentence, 1, field, sizeof(field)) && strlen(field) > 0) {
        nmea_parse_time(field, data);
    }

    // Field 2: Status (A=active/valid, V=void/invalid)
    nmea_get_field(sentence, 2, field, sizeof(field));
    bool status_valid = (field[0] == 'A');

    if (!status_valid) {
        data->position_valid = false;
        return true;  // Sentence is valid, but no position fix
    }

    // Field 3: Latitude
    double lat = 0;
    if (nmea_get_field(sentence, 3, field, sizeof(field)) && strlen(field) > 0) {
        lat = atof(field);
    }

    // Field 4: Latitude direction (N/S)
    nmea_get_field(sentence, 4, field, sizeof(field));
    char lat_dir = field[0];

    // Field 5: Longitude
    double lon = 0;
    if (nmea_get_field(sentence, 5, field, sizeof(field)) && strlen(field) > 0) {
        lon = atof(field);
    }

    // Field 6: Longitude direction (E/W)
    nmea_get_field(sentence, 6, field, sizeof(field));
    char lon_dir = field[0];

    // Convert to decimal degrees
    data->latitude = nmea_coord_to_decimal(lat, lat_dir);
    data->longitude = nmea_coord_to_decimal(lon, lon_dir);
    data->position_valid = true;

    // Field 7: Speed in knots
    if (nmea_get_field(sentence, 7, field, sizeof(field)) && strlen(field) > 0) {
        data->speed_knots = atof(field);
    }

    // Field 8: Course
    if (nmea_get_field(sentence, 8, field, sizeof(field)) && strlen(field) > 0) {
        data->course = atof(field);
    }

    // Field 9: Date
    if (nmea_get_field(sentence, 9, field, sizeof(field)) && strlen(field) > 0) {
        nmea_parse_date(field, data);
    }

    return true;
}

/**
 * @brief Parse GPGGA sentence
 *
 * Format: $GPGGA,time,lat,N/S,lon,E/W,quality,sats,hdop,alt,M,geoid,M,age,station*checksum
 * Example: $GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47
 */
static bool nmea_parse_gpgga(const char *sentence, gps_data_t *data)
{
    char field[20];

    // Field 1: Time
    if (nmea_get_field(sentence, 1, field, sizeof(field)) && strlen(field) > 0) {
        nmea_parse_time(field, data);
    }

    // Field 2: Latitude
    double lat = 0;
    if (nmea_get_field(sentence, 2, field, sizeof(field)) && strlen(field) > 0) {
        lat = atof(field);
    }

    // Field 3: Latitude direction (N/S)
    nmea_get_field(sentence, 3, field, sizeof(field));
    char lat_dir = field[0];

    // Field 4: Longitude
    double lon = 0;
    if (nmea_get_field(sentence, 4, field, sizeof(field)) && strlen(field) > 0) {
        lon = atof(field);
    }

    // Field 5: Longitude direction (E/W)
    nmea_get_field(sentence, 5, field, sizeof(field));
    char lon_dir = field[0];

    // Field 6: Fix quality
    nmea_get_field(sentence, 6, field, sizeof(field));
    data->fix_quality = (gps_fix_quality_t)atoi(field);

    // If no fix, position is invalid
    if (data->fix_quality == GPS_FIX_INVALID) {
        data->position_valid = false;
        return true;
    }

    // Convert to decimal degrees
    data->latitude = nmea_coord_to_decimal(lat, lat_dir);
    data->longitude = nmea_coord_to_decimal(lon, lon_dir);
    data->position_valid = true;

    // Field 7: Number of satellites
    if (nmea_get_field(sentence, 7, field, sizeof(field)) && strlen(field) > 0) {
        data->satellites = atoi(field);
    }

    // Field 8: HDOP
    if (nmea_get_field(sentence, 8, field, sizeof(field)) && strlen(field) > 0) {
        data->hdop = atof(field);
    }

    // Field 9: Altitude
    if (nmea_get_field(sentence, 9, field, sizeof(field)) && strlen(field) > 0) {
        data->altitude = atof(field);
        data->altitude_valid = true;
    }

    return true;
}

bool nmea_parse_sentence(const char *sentence, gps_data_t *data)
{
    if (sentence == NULL || data == NULL) {
        return false;
    }

    // Validate checksum first
    if (!nmea_validate_checksum(sentence)) {
        ESP_LOGW(TAG, "Invalid checksum");
        return false;
    }

    // Determine sentence type and parse
    if (strncmp(sentence, GPRMC_PREFIX, strlen(GPRMC_PREFIX)) == 0 ||
        strncmp(sentence, GNRMC_PREFIX, strlen(GNRMC_PREFIX)) == 0) {
        return nmea_parse_gprmc(sentence, data);
    }
    else if (strncmp(sentence, GPGGA_PREFIX, strlen(GPGGA_PREFIX)) == 0 ||
             strncmp(sentence, GNGGA_PREFIX, strlen(GNGGA_PREFIX)) == 0) {
        return nmea_parse_gpgga(sentence, data);
    }
    else {
        // Unknown or unsupported sentence type
        ESP_LOGD(TAG, "Unsupported sentence type: %.6s", sentence);
        return false;
    }
}
