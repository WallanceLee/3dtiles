#include "GeoidHeight.h"
#include <GeographicLib/Geoid.hpp>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <cstdlib>

namespace GeoidHeight {

bool GeoidCalculator::Initialize(GeoidModel model, const std::string& geoidPath) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (model == GeoidModel::NONE) {
        initialized_ = false;
        model_ = GeoidModel::NONE;
        geoid_ = nullptr;
        spdlog::info("[GeoidHeight] Geoid model set to NONE, no height conversion will be applied");
        return true;
    }
    
    std::string geoidName;
    switch (model) {
        case GeoidModel::EGM84:
            geoidName = "egm84-15";
            break;
        case GeoidModel::EGM96:
            geoidName = "egm96-5";
            break;
        case GeoidModel::EGM2008:
            geoidName = "egm2008-5";
            break;
        default:
            spdlog::error("[GeoidHeight] Unknown geoid model");
            return false;
    }
    
    try {
        std::string actualPath = geoidPath;
        if (actualPath.empty()) {
            actualPath = GetDefaultGeoidDataPath();
        }
        
        spdlog::info("[GeoidHeight] Initializing geoid model: {} with path: {}", geoidName, actualPath);
        
        auto* g = new GeographicLib::Geoid(geoidName, actualPath, true, true);
        
        if (geoid_) {
            delete static_cast<GeographicLib::Geoid*>(geoid_);
        }
        geoid_ = g;
        model_ = model;
        initialized_ = true;
        
        spdlog::info("[GeoidHeight] Geoid model {} initialized successfully", geoidName);
        spdlog::info("[GeoidHeight] Description: {}", g->Description());
        spdlog::info("[GeoidHeight] DateTime: {}", g->DateTime());
        spdlog::info("[GeoidHeight] Interpolation: {}", g->Interpolation());
        spdlog::info("[GeoidHeight] MaxError: {} m", g->MaxError());
        spdlog::info("[GeoidHeight] RMSError: {} m", g->RMSError());
        
        return true;
    } catch (const std::exception& e) {
        spdlog::error("[GeoidHeight] Failed to initialize geoid model {}: {}", geoidName, e.what());
        initialized_ = false;
        geoid_ = nullptr;
        return false;
    }
}

std::optional<double> GeoidCalculator::GetGeoidHeight(double lat, double lon) const {
    if (!initialized_ || !geoid_) {
        return std::nullopt;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    try {
        auto* g = static_cast<GeographicLib::Geoid*>(geoid_);
        return (*g)(lat, lon);
    } catch (const std::exception& e) {
        spdlog::error("[GeoidHeight] Failed to get geoid height at ({}, {}): {}", lat, lon, e.what());
        return std::nullopt;
    }
}

double GeoidCalculator::ConvertOrthometricToEllipsoidal(double lat, double lon, double orthometricHeight) const {
    auto geoidHeight = GetGeoidHeight(lat, lon);
    if (geoidHeight) {
        return orthometricHeight + geoidHeight.value();
    }
    return orthometricHeight;
}

double GeoidCalculator::ConvertEllipsoidalToOrthometric(double lat, double lon, double ellipsoidalHeight) const {
    auto geoidHeight = GetGeoidHeight(lat, lon);
    if (geoidHeight) {
        return ellipsoidalHeight - geoidHeight.value();
    }
    return ellipsoidalHeight;
}

std::string GeoidCalculator::GeoidModelToString(GeoidModel model) {
    switch (model) {
        case GeoidModel::NONE: return "none";
        case GeoidModel::EGM84: return "egm84";
        case GeoidModel::EGM96: return "egm96";
        case GeoidModel::EGM2008: return "egm2008";
        default: return "unknown";
    }
}

GeoidModel GeoidCalculator::StringToGeoidModel(const std::string& str) {
    if (str == "egm84" || str == "EGM84") return GeoidModel::EGM84;
    if (str == "egm96" || str == "EGM96") return GeoidModel::EGM96;
    if (str == "egm2008" || str == "EGM2008") return GeoidModel::EGM2008;
    return GeoidModel::NONE;
}

std::string GeoidCalculator::GetDefaultGeoidDataPath() {
    const char* envPath = std::getenv("GEOGRAPHICLIB_GEOID_PATH");
    if (envPath && envPath[0] != '\0') {
        return std::string(envPath);
    }
    
    const char* dataPath = std::getenv("GEOGRAPHICLIB_DATA");
    if (dataPath && dataPath[0] != '\0') {
        return std::string(dataPath) + "/geoids";
    }
    
#ifdef _WIN32
    return "C:/ProgramData/GeographicLib/geoids";
#else
    return "/usr/local/share/GeographicLib/geoids";
#endif
}

GeoidCalculator& GetGlobalGeoidCalculator() {
    static GeoidCalculator instance;
    return instance;
}

bool InitializeGlobalGeoidCalculator(GeoidModel model, const std::string& geoidPath) {
    return GetGlobalGeoidCalculator().Initialize(model, geoidPath);
}

}
