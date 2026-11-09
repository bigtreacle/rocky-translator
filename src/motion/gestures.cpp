#include "gestures.h"

#include <math.h>

namespace motion {
namespace {

// Thresholds
constexpr float STILL_VAR = 0.0015f;     // variance of |a| (g^2) below which we are "still"
constexpr uint32_t STILL_MS = 2000;      // still this long before a pick-up can count
constexpr float PICKUP_DEV_G = 0.25f;    // |a - gravity| that counts as a pick-up
constexpr float PICKUP_ANGLE_DEG = 25.0f;
constexpr uint32_t PICKUP_COOLDOWN_MS = 2000;
constexpr float SHAKE_G = 1.2f;          // high-passed magnitude per hit
constexpr int SHAKE_HITS = 3;
constexpr uint32_t SHAKE_WINDOW_MS = 800;
constexpr uint32_t SHAKE_GAP_MS = 60;
constexpr uint32_t SHAKE_COOLDOWN_MS = 2000;
constexpr float FACE_DOWN_Z = 0.8f;      // screen-down: z positive (screen-up is -1)
constexpr uint32_t FACE_DOWN_MS = 1000;
constexpr float FACE_UP_Z = -0.5f;
constexpr uint32_t FACE_UP_MS = 300;

float g_lastHp = 0;
hal::Accel g_grav;          // low-passed gravity estimate
bool g_gravInit = false;
float g_mag = 1;
float g_magMean = 1, g_magVar = 0;  // running stats of |a|
uint32_t g_stillSince = 0;
bool g_still = false;
uint32_t g_lastPickup = 0;
uint32_t g_lastShake = 0;
uint32_t g_hitTimes[8];
int8_t g_hitSign[8];      // +1/-1 along the dominant axis of each hit
uint8_t g_hitAxis[8];     // 0=x 1=y 2=z
int g_hitCount = 0;
uint32_t g_lastHit = 0;
uint32_t g_suppressShakeUntil = 0;  // after a pick-up or a flip, hits are not shakes
bool g_faceDown = false;
uint32_t g_faceDownSince = 0, g_faceUpSince = 0;

float dot(const hal::Accel &a, const hal::Accel &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
float norm(const hal::Accel &a) { return sqrtf(dot(a, a)); }

}  // namespace

void begin() {
  g_gravInit = false;
  g_still = false;
  g_hitCount = 0;
  g_faceDown = false;
}

Gesture update(const hal::Accel &a, uint32_t now) {
  g_mag = norm(a);
  if (!g_gravInit) {
    g_grav = a;
    g_gravInit = true;
    g_magMean = g_mag;
    g_stillSince = now;
  }
  // Running mean/variance of |a| over ~1 s (50 Hz -> alpha 0.02)
  // ~0.4 s time constant at 50 Hz: a shake's variance decays in a few seconds, so
  // the board counts as resting again soon after it is put down.
  float d = g_mag - g_magMean;
  g_magMean += 0.05f * d;
  g_magVar += 0.05f * (d * d - g_magVar);

  // Face-down / face-up (uses raw z; gravity is ~1 g when resting)
  Gesture out = Gesture::None;
  if (!g_faceDown) {
    if (a.z > FACE_DOWN_Z) {
      if (!g_faceDownSince) g_faceDownSince = now;
      if (now - g_faceDownSince >= FACE_DOWN_MS) {
        g_faceDown = true;
        g_faceUpSince = 0;
        out = Gesture::FaceDown;
      }
    } else {
      g_faceDownSince = 0;
    }
  } else {
    if (a.z < FACE_UP_Z) {
      if (!g_faceUpSince) g_faceUpSince = now;
      if (now - g_faceUpSince >= FACE_UP_MS) {
        g_faceDown = false;
        g_faceDownSince = 0;
        out = Gesture::FaceUp;
      }
    } else {
      g_faceUpSince = 0;
    }
  }

  if (out == Gesture::FaceUp || out == Gesture::FaceDown) g_suppressShakeUntil = now + 1500;

  hal::Accel hp{a.x - g_grav.x, a.y - g_grav.y, a.z - g_grav.z};
  float hpMag = norm(hp);
  g_lastHp = hpMag;

  // Pick-up first: the board rested for a while, then gravity leaves its resting
  // direction or the acceleration clearly deviates. Stillness is judged on the
  // variance of |a| alone so desk vibration does not keep resetting it.
  bool stillNow = g_magVar < STILL_VAR;
  if (stillNow) {
    if (!g_still) {
      g_still = true;
      g_stillSince = now;
    }
  } else if (g_still) {
    bool restedLongEnough = now - g_stillSince >= STILL_MS;
    float cosA = dot(a, g_grav) / (norm(a) * norm(g_grav) + 1e-6f);
    float angle = acosf(cosA > 1 ? 1 : (cosA < -1 ? -1 : cosA)) * 180.0f / (float)M_PI;
    if (restedLongEnough && (hpMag > PICKUP_DEV_G || angle > PICKUP_ANGLE_DEG) &&
        now - g_lastPickup > PICKUP_COOLDOWN_MS && out == Gesture::None) {
      g_lastPickup = now;
      g_suppressShakeUntil = now + 1500;  // lifting produces hits that are not a shake
      g_hitCount = 0;
      out = Gesture::PickedUp;
    }
    g_still = false;
  }

  // Shake: hits of high-passed acceleration that alternate direction along one
  // axis. A rotation (turning the board over) moves gravity one way and settles,
  // so it produces hits of one sign and does not count.
  if (hpMag > SHAKE_G && now - g_lastHit >= SHAKE_GAP_MS && now >= g_suppressShakeUntil) {
    g_lastHit = now;
    float ax = fabsf(hp.x), ay = fabsf(hp.y), az = fabsf(hp.z);
    uint8_t axis = ax >= ay && ax >= az ? 0 : (ay >= az ? 1 : 2);
    float v = axis == 0 ? hp.x : (axis == 1 ? hp.y : hp.z);
    if (g_hitCount < 8) {
      g_hitTimes[g_hitCount] = now;
      g_hitSign[g_hitCount] = v >= 0 ? 1 : -1;
      g_hitAxis[g_hitCount] = axis;
      g_hitCount++;
    }
  }
  int keep = 0;
  for (int i = 0; i < g_hitCount; i++)
    if (now - g_hitTimes[i] <= SHAKE_WINDOW_MS) {
      g_hitTimes[keep] = g_hitTimes[i];
      g_hitSign[keep] = g_hitSign[i];
      g_hitAxis[keep] = g_hitAxis[i];
      keep++;
    }
  g_hitCount = keep;
  if (out == Gesture::None && g_hitCount >= SHAKE_HITS && now - g_lastShake > SHAKE_COOLDOWN_MS) {
    // count alternations along the most common axis
    int alternations = 0;
    for (int i = 1; i < g_hitCount; i++)
      if (g_hitAxis[i] == g_hitAxis[i - 1] && g_hitSign[i] != g_hitSign[i - 1]) alternations++;
    if (alternations >= SHAKE_HITS - 1) {
      g_lastShake = now;
      g_hitCount = 0;
      out = Gesture::Shaken;
    }
  }

  // Gravity follows slowly while still, faster otherwise (so it tracks a new resting pose)
  float k = stillNow ? 0.02f : 0.1f;
  g_grav.x += k * (a.x - g_grav.x);
  g_grav.y += k * (a.y - g_grav.y);
  g_grav.z += k * (a.z - g_grav.z);
  return out;
}

Debug debug(uint32_t now) {
  return Debug{g_mag, g_lastHp, g_magVar, g_still, g_hitCount, g_still ? now - g_stillSince : 0};
}
bool isStill() { return g_still; }
bool isFaceDown() { return g_faceDown; }
float magnitude() { return g_mag; }

}  // namespace motion
