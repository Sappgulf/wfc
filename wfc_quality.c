#include "wfc_quality.h"

#include <math.h>
#include <string.h>

double wfc_quality_clamp(double value) {
    if (!isfinite(value)) return 0.0;
    return value < 0.0 ? 0.0 : value > 1.0 ? 1.0 : value;
}

double wfc_quality_signed_clamp(double value) {
    if (!isfinite(value)) return 0.0;
    return value < -1.0 ? -1.0 : value > 1.0 ? 1.0 : value;
}

WfcQualityProfile wfc_quality_profile_for_mode(const char *mode) {
    if (mode && !strcmp(mode, "streets"))
        return (WfcQualityProfile){"streets", 0.30, 0.18, 0.14, 0.08, 0.08, 0.06, 0.16};
    if (mode && !strcmp(mode, "neurons"))
        return (WfcQualityProfile){"neurons", 0.24, 0.05, 0.12, 0.15, 0.08, 0.06, 0.30};
    if (mode && !strcmp(mode, "mycelium"))
        return (WfcQualityProfile){"mycelium", 0.24, 0.04, 0.16, 0.14, 0.12, 0.06, 0.24};
    if (mode && !strcmp(mode, "delta"))
        return (WfcQualityProfile){"delta", 0.27, 0.12, 0.14, 0.08, 0.10, 0.05, 0.24};
    if (mode && !strcmp(mode, "rail"))
        return (WfcQualityProfile){"rail", 0.30, 0.16, 0.14, 0.07, 0.09, 0.06, 0.18};
    return (WfcQualityProfile){"balanced", 0.30, 0.03, 0.18, 0.16, 0.16, 0.05, 0.12};
}

double wfc_quality_score(WfcQualityProfile profile,
                         WfcQualityComponents components) {
    return wfc_quality_clamp(
        profile.validity * components.validity +
        profile.boundary * components.boundary +
        profile.coverage * components.coverage +
        profile.diversity * components.diversity +
        profile.smoothness * components.smoothness +
        profile.stability * components.stability +
        profile.topology * components.topology);
}
