#ifndef WFC_QUALITY_H
#define WFC_QUALITY_H

#include <stdbool.h>

typedef struct {
    double validity;
    double boundary;
    double coverage;
    double diversity;
    double smoothness;
    double stability;
    double topology;
} WfcQualityComponents;

typedef struct {
    const char *focus;
    double validity;
    double boundary;
    double coverage;
    double diversity;
    double smoothness;
    double stability;
    double topology;
} WfcQualityProfile;

double wfc_quality_clamp(double value);
double wfc_quality_signed_clamp(double value);
WfcQualityProfile wfc_quality_profile_for_mode(const char *mode);
double wfc_quality_score(WfcQualityProfile profile,
                         WfcQualityComponents components);

#endif
