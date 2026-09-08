#ifndef HP3_AIM_POLICY_H
#define HP3_AIM_POLICY_H
// Pure geometry from HP3's SelectCursor / SpellCursor bytecode. No UE ABI.
#include <cmath>
#include <cstring>
namespace hp3aim {
static inline bool samePackage(const char *actor, const char *camera) {
    const char *a = std::strchr(actor, ' '), *c = std::strchr(camera, ' ');
    if (!a || !c) return false;
    ++a; ++c;
    const char *ae = std::strchr(a, '.'), *ce = std::strchr(c, '.');
    return ae && ce && ae > a && ae-a == ce-c && !std::strncmp(a,c,ae-a);
}
static inline float range(float value) {
    return std::isfinite(value) && value > 0 && value <= 10000 ? value : 770.0f;
}
static inline void direction(const int rot[3], float out[3]) {
    const double unit = 6.283185307179586 / 65536.0;
    double p=rot[0]*unit, y=rot[1]*unit;
    out[0]=(float)(std::cos(p)*std::cos(y));
    out[1]=(float)(std::cos(p)*std::sin(y)); out[2]=(float)std::sin(p);
}
// UE FVector >> FRotator (pitch/yaw/roll), not a world-space offset.
static inline void rotate(const float v[3], const int rot[3], float out[3]) {
    const double u=6.283185307179586/65536.0;
    double cp=std::cos(rot[0]*u),sp=std::sin(rot[0]*u),cy=std::cos(rot[1]*u),sy=std::sin(rot[1]*u),cr=std::cos(rot[2]*u),sr=std::sin(rot[2]*u);
    out[0]=(float)(v[0]*cp*cy+v[1]*(sr*sp*cy-cr*sy)+v[2]*(-cr*sp*cy-sr*sy));
    out[1]=(float)(v[0]*cp*sy+v[1]*(sr*sp*sy+cr*cy)+v[2]*(-cr*sp*sy+sr*cy));
    out[2]=(float)(v[0]*sp-v[1]*sr*cp+v[2]*cr*cp);
}
static inline float rayLength(const float cam[3], const float pawn[3], const float dir[3], float los) {
    float add=0; for(int k=0;k<3;k++) add+=(pawn[k]-cam[k])*dir[k];
    return std::fmax(1.0f,range(los)+add);
}
static inline float gestureDistance(float collisionRadius, float sizeModifier, float gestureOffset) {
    return collisionRadius*1.1f*sizeModifier+2.0f+gestureOffset;
}
static inline void gesturePoint(const float centre[3], const float dir[3], float distance, float out[3]) {
    for(int k=0;k<3;k++) out[k]=centre[k]-dir[k]*distance;
}
// Used to validate mod-owned FX before touching them (object slots can recycle).
static inline bool sameIdentity(const void *expected, const void *slot, const void *expectedClass,
                                const void *actualClass, const char *expectedName, const char *name) {
    return expected && expected==slot && expectedClass && expectedClass==actualClass && !std::strcmp(expectedName,name);
}
}
#endif
