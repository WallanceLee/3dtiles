#pragma once
/* vcpkg path */
#include <ogr_spatialref.h>
#include <ogrsf_frmts.h>
#include <memory>
#include <string>

#include "glm/glm.hpp"

struct OGRCTDeleter
{
    void operator()(OGRCoordinateTransformation* pCT) const
    {
        if (pCT != nullptr)
        {
            OGRCoordinateTransformation::DestroyCT(pCT);
        }
    }
};

class GeoTransform
{
public:
    static inline thread_local std::unique_ptr<OGRCoordinateTransformation, OGRCTDeleter> pOgrCT = nullptr;

    static inline double OriginX = 0.0;
    static inline double OriginY = 0.0;
    static inline double OriginZ = 0.0;

    static inline double GeoOriginLon = 0.0;
    static inline double GeoOriginLat = 0.0;
    static inline double GeoOriginHeight = 0.0;

    static inline bool IsENU = false;
    static inline glm::dmat4 EcefToEnuMatrix = glm::dmat4(1);

    static inline int SourceEPSG_ = 0;
    static inline std::string SourceWKT_;
    static inline bool GlobalInitialized_ = false;

    static void EnsureThreadTransform();

    static glm::dmat4 CalcEnuToEcefMatrix(double lnt, double lat, double height_min);

    static glm::dvec3 CartographicToEcef(double lnt, double lat, double height);

    static void Init(OGRCoordinateTransformation *pOgrCT, double *Origin);

    static void SetGeographicOrigin(double lon, double lat, double height);
};