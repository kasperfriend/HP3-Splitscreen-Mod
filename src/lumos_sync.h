#ifndef HP3_LUMOS_SYNC_H
#define HP3_LUMOS_SYNC_H

#include <cstdint>
#include <cmath>
#include <cstring>

namespace hp3lumos {

// Default duration for Lumos in milliseconds if not specified by game (typically 20-30s in HP3)
static const std::uint32_t DefaultLumosDurationMs = 25000;
// Proximity radius (units) to detect and activate Lumos triggers/sparkles around secret walls
static const float DefaultTriggerRadius = 350.0f;
static const float DefaultTriggerHeight = 150.0f;

struct LightProperties {
    unsigned char lightType;        // 0 = LT_None, 1 = LT_Steady
    unsigned char lightEffect;
    unsigned char lightBrightness;  // 0 .. 255
    unsigned char lightHue;
    unsigned char lightSaturation;
    float lightRadius;
    bool bHidden;
    bool bCollideActors;
};

inline bool isLightActive(bool bHidden, unsigned char lightType, unsigned char lightBrightness)
{
    return !bHidden && (lightType != 0) && (lightBrightness > 0);
}

struct State {
    bool active;
    int sourcePlayer;
    std::uint32_t startTick;
    std::uint32_t expireTick;
    LightProperties sourceLight;

    State()
        : active(false)
        , sourcePlayer(-1)
        , startTick(0)
        , expireTick(0)
    {
        std::memset(&sourceLight, 0, sizeof(sourceLight));
    }

    void activate(int player, std::uint32_t nowMs, std::uint32_t durationMs = DefaultLumosDurationMs)
    {
        active = true;
        sourcePlayer = player;
        startTick = nowMs;
        expireTick = nowMs + durationMs;
    }

    void refresh(std::uint32_t nowMs, std::uint32_t durationMs = DefaultLumosDurationMs)
    {
        if (active) {
            std::uint32_t newExpire = nowMs + durationMs;
            if (newExpire > expireTick) {
                expireTick = newExpire;
            }
        } else {
            activate(sourcePlayer >= 0 ? sourcePlayer : 0, nowMs, durationMs);
        }
    }

    void deactivate()
    {
        active = false;
        sourcePlayer = -1;
        startTick = 0;
        expireTick = 0;
        std::memset(&sourceLight, 0, sizeof(sourceLight));
    }

    bool isExpired(std::uint32_t nowMs) const
    {
        if (!active) return true;
        // Handle 32-bit tick wrapping safely
        return static_cast<std::int32_t>(nowMs - expireTick) >= 0;
    }

    float remainingSeconds(std::uint32_t nowMs) const
    {
        if (!active || isExpired(nowMs)) return 0.0f;
        return static_cast<float>(expireTick - nowMs) / 1000.0f;
    }

    bool update(std::uint32_t nowMs)
    {
        if (active && isExpired(nowMs)) {
            deactivate();
            return false;
        }
        return active;
    }
};

inline bool isWithinCylinder(const float pos[3], const float center[3], float radius, float height)
{
    float dx = pos[0] - center[0];
    float dy = pos[1] - center[1];
    float dz = pos[2] - center[2];
    if (std::fabs(dz) > height) return false;
    return (dx * dx + dy * dy) <= (radius * radius);
}

inline bool anyPlayerNearTrigger(const float playerPositions[][3], int numPlayers,
                                 const float triggerLoc[3], float radius, float height)
{
    for (int i = 0; i < numPlayers; ++i) {
        if (isWithinCylinder(playerPositions[i], triggerLoc, radius, height)) {
            return true;
        }
    }
    return false;
}

inline bool shouldSecretWallBePassable(bool lumosActive, bool playerNear)
{
    return lumosActive && playerNear;
}

} // namespace hp3lumos

#endif // HP3_LUMOS_SYNC_H
