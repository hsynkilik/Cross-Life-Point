#pragma once

#include <atomic>
#include <cstdint>

#include "activities/Activity.h"

// Two-player life counter (Flesh and Blood). Portrait, players face each
// other across the long sides: player 1 owns the top half, player 2 the bottom.
// Front keys 1-4 (left to right): P2 -, P2 +, P1 +, P1 -.
// Short press = 1, hold >= 600 ms = 5 (applied once on release).
// Hold the top side key 1 s: show / hide each player's history (own changes,
// newest 9). Hold the bottom side key 2 s: reset both to 40 and clear the
// history. Sleep only after 20 min without input. Short presses 1,4,1,4 within 3 s:
// undo those four presses and return home.
// 2 s after a player's last press, a small "+N"/"-N" under the total shows the
// sum of that burst of presses; it stays until that player changes again.
// The screen only refreshes when a total or a shown change actually changes.
// A player at 0 gets a skull ("YOU LOSE") at once; 2 s later the other player
// gets a trophy with rays ("YOU WIN").
// Touch boards (X4 Pro): tap the +/- band at either end instead of the front
// keys (- on each player's left, + on their right; hold = 5), hold a Left/Right
// nav button 1 s to reset, and press Home to leave.
class LifePointsActivity final : public Activity {
 public:
  explicit LifePointsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("LifePoints", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override;

 private:
  static constexpr int FRONT_KEYS = 4;

  void applyPress(int key, bool longPress, unsigned long now);
  void handleExitSequence(int key, bool longPress, unsigned long now);
  void markChanged(bool forceFull);
  void updateGameOver(unsigned long now);
  int touchZoneAt(int x, int y) const;

  GfxRenderer::Orientation savedOrientation = GfxRenderer::Orientation::Portrait;
  unsigned long pressStart[FRONT_KEYS] = {};
  bool pressActive[FRONT_KEYS] = {};
  // Side keys: 0 = history (X4 top side key / X4 Pro Right), 1 = reset
  // (X4 bottom side key / X4 Pro Left).
  unsigned long sideStart[2] = {};
  bool sideActive[2] = {};
  bool sideDone[2] = {};
  bool showHistory = false;
  unsigned long lastInput = 0;  // last key press or release, for the sleep delay
  int touchZone = -1;           // zone the touch started in on touch boards, -1 if none
  bool touchInZone = false;     // finger still inside that zone

  // Exit sequence 1,4,1,4: step count, start time, and life totals before it.
  int seqStep = 0;
  unsigned long seqStart = 0;
  int seqLife1 = 0;
  int seqLife2 = 0;

  // Per player (0 = top, 1 = bottom): change summed since the burst started,
  // the change currently drawn, and when the last press landed.
  int pendingDelta[2] = {};
  bool deltaPending[2] = {};
  int shownDelta[2] = {};
  unsigned long lastChange[2] = {};

  // Game over: exactly one player at 0, since when, and whether the
  // winner's trophy is already drawn.
  bool gameOver = false;
  unsigned long gameOverSince = 0;
  bool trophyShown = false;

  std::atomic<bool> fullRefreshPending{true};
};
