#include "GeoTransform.h"
#include "extern.h"
#include "GeoidHeight.h"
#include <cstdio>
#include <glm/glm.hpp>

// Geoid height conversion functions implementation
extern "C" bool init_geoid(const char* model, const char* geoid_path) {
    GeoidHeight::GeoidModel geoidModel = GeoidHeight::GeoidCalculator::StringToGeoidModel(std::string(model));
    return GeoidHeight::InitializeGlobalGeoidCalculator(geoidModel, std::string(geoid_path));
}

extern "C" double get_geoid_height(double lat, double lon) {
    auto height = GeoidHeight::GetGlobalGeoidCalculator().GetGeoidHeight(lat, lon);
    return height.value_or(0.0);
}

extern "C" double orthometric_to_ellipsoidal(double lat, double lon, double orthometric_height) {
    return GeoidHeight::GetGlobalGeoidCalculator().ConvertOrthometricToEllipsoidal(lat, lon, orthometric_height);
}

extern "C" double ellipsoidal_to_orthometric(double lat, double lon, double ellipsoidal_height) {
    return GeoidHeight::GetGlobalGeoidCalculator().ConvertEllipsoidalToOrthometric(lat, lon, ellipsoidal_height);
}

extern "C" bool is_geoid_initialized() {
    return GeoidHeight::GetGlobalGeoidCalculator().IsInitialized();
}

extern "C" double get_geo_origin_height() {
    return GeoTransform::GeoOriginHeight;
}

glm::dmat4 GeoTransform::CalcEnuToEcefMatrix(double lnt, double lat, double height_min)
{
    const double pi = std::acos(-1.0);
    const double a = 6378137.0;                  // WGS84 semi-major axis
    const double f = 1.0 / 298.257223563;        // WGS84 flattening
    const double e2 = f * (2.0 - f);             // eccentricity squared

    double lon = lnt * pi / 180.0;
    double phi = lat * pi / 180.0;

    double sinPhi = std::sin(phi), cosPhi = std::cos(phi);
    double sinLon = std::sin(lon), cosLon = std::cos(lon);

    double N = a / std::sqrt(1.0 - e2 * sinPhi * sinPhi);
    double x0 = (N + height_min) * cosPhi * cosLon;
    double y0 = (N + height_min) * cosPhi * sinLon;
    double z0 = (N * (1.0 - e2) + height_min) * sinPhi;

    // ENU basis vectors expressed in ECEF
    glm::dvec3 east(-sinLon,           cosLon,            0.0);
    glm::dvec3 north(-sinPhi * cosLon, -sinPhi * sinLon,  cosPhi);
    glm::dvec3 up(   cosPhi * cosLon,   cosPhi * sinLon,  sinPhi);

    // Build ENU->ECEF (rotation + translation), column-major
    glm::dmat4 T(1.0);
    T[0] = glm::dvec4(east,  0.0);
    T[1] = glm::dvec4(north, 0.0);
    T[2] = glm::dvec4(up,    0.0);
    T[3] = glm::dvec4(x0, y0, z0, 1.0);
    return T;
}

glm::dvec3 GeoTransform::CartographicToEcef(double lnt, double lat, double height)
{
    const double pi = std::acos(-1.0);
    const double a = 6378137.0;                  // WGS84 semi-major axis
    const double f = 1.0 / 298.257223563;        // WGS84 flattening
    const double e2 = f * (2.0 - f);             // eccentricity squared

    double lon = lnt * pi / 180.0;
    double phi = lat * pi / 180.0;

    double sinPhi = std::sin(phi), cosPhi = std::cos(phi);
    double sinLon = std::sin(lon), cosLon = std::cos(lon);

    double N = a / std::sqrt(1.0 - e2 * sinPhi * sinPhi);
    double x = (N + height) * cosPhi * cosLon;
    double y = (N + height) * cosPhi * sinLon;
    double z = (N * (1.0 - e2) + height) * sinPhi;

    return { x, y, z };
}

void GeoTransform::Init(OGRCoordinateTransformation *pOgrCT, double *Origin)
{
    GeoTransform::pOgrCT.reset(pOgrCT);
    GeoTransform::OriginX = Origin[0];
    GeoTransform::OriginY = Origin[1];
    GeoTransform::OriginZ = Origin[2];
    GeoTransform::IsENU = false;

    glm::dvec3 origin = { GeoTransform::OriginX, GeoTransform::OriginY, GeoTransform::OriginZ };
    glm::dvec3 origin_cartographic = origin;
    fprintf(stderr, "[GeoTransform] ENU origin: x=%.8f y=%.8f z=%.3f\n", origin.x, origin.y, origin.z);

    if (GeoTransform::pOgrCT)
    {
        GeoTransform::pOgrCT->Transform(1, &origin_cartographic.x, &origin_cartographic.y, &origin_cartographic.z);
    }

    fprintf(stderr, "[GeoTransform] Cartographic origin: lon=%.10f lat=%.10f h=%.3f\n", origin_cartographic.x, origin_cartographic.y, origin_cartographic.z);

    // Apply geoid height correction if geoid is initialized
    // This converts orthometric height (e.g., China 1985) to ellipsoidal height (WGS84)
    double final_height = origin_cartographic.z;
    if (is_geoid_initialized()) {
        double geoid_height = get_geoid_height(origin_cartographic.y, origin_cartographic.x);
        double original_height = origin_cartographic.z;
        final_height = orthometric_to_ellipsoidal(origin_cartographic.y, origin_cartographic.x, origin_cartographic.z);
        fprintf(stderr, "[GeoTransform] Geoid correction applied: orthometric=%.3f + geoid=%.3f = ellipsoidal=%.3f\n",
                original_height, geoid_height, final_height);
    }

    GeoTransform::GeoOriginLon = origin_cartographic.x;
    GeoTransform::GeoOriginLat = origin_cartographic.y;
    GeoTransform::GeoOriginHeight = final_height;

    glm::dmat4 EnuToEcefMatrix = GeoTransform::CalcEnuToEcefMatrix(origin_cartographic.x, origin_cartographic.y, final_height);
    GeoTransform::EcefToEnuMatrix = glm::inverse(EnuToEcefMatrix);

    GeoTransform::GlobalInitialized_ = true;
}

void GeoTransform::EnsureThreadTransform()
{
    if (GeoTransform::pOgrCT) return;
    if (!GeoTransform::GlobalInitialized_) return;
    if (GeoTransform::IsENU) return;

    OGRSpatialReference outRs;
    outRs.importFromEPSG(4326);
    outRs.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);

    OGRSpatialReference inRs;
    if (GeoTransform::SourceEPSG_ > 0) {
        inRs.importFromEPSG(GeoTransform::SourceEPSG_);
        inRs.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    } else if (!GeoTransform::SourceWKT_.empty()) {
        inRs.importFromWkt(GeoTransform::SourceWKT_.c_str());
        inRs.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    } else {
        return;
    }

    OGRCoordinateTransformation* poCT = OGRCreateCoordinateTransformation(&inRs, &outRs);
    if (poCT) {
        GeoTransform::pOgrCT.reset(poCT);
        fprintf(stderr, "[GeoTransform] Worker thread: created per-thread OGR transform\n");
    } else {
        fprintf(stderr, "[GeoTransform] Worker thread: FAILED to create OGR transform\n");
    }
}

void GeoTransform::SetGeographicOrigin(double lon, double lat, double height)
{
    GeoTransform::GeoOriginLon = lon;
    GeoTransform::GeoOriginLat = lat;
    GeoTransform::GeoOriginHeight = height;
    GeoTransform::IsENU = true;

    // Recalculate ENU<->ECEF matrices using the geographic origin
    glm::dmat4 EnuToEcefMatrix = GeoTransform::CalcEnuToEcefMatrix(lon, lat, height);
    GeoTransform::EcefToEnuMatrix = glm::inverse(EnuToEcefMatrix);

    fprintf(stderr, "[GeoTransform] Geographic origin set: lon=%.10f lat=%.10f h=%.3f\n", lon, lat, height);
}
