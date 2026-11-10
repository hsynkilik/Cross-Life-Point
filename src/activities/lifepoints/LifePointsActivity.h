#pragma once

#include <atomic>
#include <cstdint>

#include "activities/Activity.h"

// Two-player life counter (Flesh and Blood). Portrait, players face each
// other across the long sides: player 1 owns the top half, player 2 the bottom.
// Front keys 1-4 (left to right): P1 -, P1 +, P2 +, P2 -.
// Short press = 1, hold >= 600 ms = 5 (applied once on release).
// Hold a side key >= 1 s: reset both to 40. Short presses 1,4,1,4 within 3 s:
// undo those four presses and return home.
class LifePointsActivity final : public Activity {
 public:
  explicit LifePointsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("LifePoints", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  static constexpr int FRONT_KEYS = 4;

  void applyPress(int key, bool longPress);
  void handleExitSequence(int key, bool longPress, unsigned long now);
  void markChanged(bool forceFull);

  GfxRenderer::Orientation savedOrientation = GfxRenderer::Orientation::Portrait;
  unsigned long pressStart[FRONT_KEYS] = {};
  bool pressActive[FRONT_KEYS] = {};
  unsigned long sideStart = 0;
  bool sideActive = false;
  bool sideResetDone = false;

  // Exit sequence 1,4,1,4: step count, start time, and life totals before it.
  int seqStep = 0;
  unsigned long seqStart = 0;
  int seqLife1 = 0;
  int seqLife2 = 0;

  int changesSinceFull = 0;
  std::atomic<bool> fullRefreshPending{true};
};
