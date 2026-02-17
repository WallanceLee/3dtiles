#pragma once

#include <string>
#include <memory>
#include <mutex>
#include <optional>

namespace GeoidHeight {

enum class GeoidModel {
    NONE,
    EGM84,
    EGM96,
    EGM2008
};

class GeoidCalculator {
public:
    GeoidCalculator() = default;
    ~GeoidCalculator() = default;
    
    GeoidCalculator(const GeoidCalculator&) = delete;
    GeoidCalculator& operator=(const GeoidCalculator&) = delete;
    
    bool Initialize(GeoidModel model, const std::string& geoidPath = "");
    
    bool IsInitialized() const { return initialized_; }
    
    GeoidModel GetModel() const { return model_; }
    
    std::optional<double> GetGeoidHeight(double lat, double lon) const;
    
    double ConvertOrthometricToEllipsoidal(double lat, double lon, double orthometricHeight) const;
    
    double ConvertEllipsoidalToOrthometric(double lat, double lon, double ellipsoidalHeight) const;
    
    static std::string GeoidModelToString(GeoidModel model);
    static GeoidModel StringToGeoidModel(const std::string& str);
    static std::string GetDefaultGeoidDataPath();

private:
    bool initialized_ = false;
    GeoidModel model_ = GeoidModel::NONE;
    mutable std::mutex mutex_;
    void* geoid_ = nullptr;
};

GeoidCalculator& GetGlobalGeoidCalculator();

bool InitializeGlobalGeoidCalculator(GeoidModel model, const std::string& geoidPath = "");

}
