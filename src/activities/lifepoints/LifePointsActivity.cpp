#include "LifePointsActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalGPIO.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace {
constexpr int START_LIFE = 40;
constexpr int MAX_LIFE = 999;
constexpr unsigned long LONG_PRESS_MS = 600;
constexpr unsigned long RESET_HOLD_MS = 1000;
constexpr unsigned long EXIT_WINDOW_MS = 3000;
constexpr unsigned long DELTA_DELAY_MS = 2000;

// Physical front keys, left to right.
constexpr uint8_t FRONT_KEY[4] = {HalGPIO::BTN_BACK, HalGPIO::BTN_CONFIRM, HalGPIO::BTN_LEFT, HalGPIO::BTN_RIGHT};
// Exit sequence: keys 1,4,1,4 (0-based 0,3,0,3).
constexpr int EXIT_SEQ[4] = {0, 3, 0, 3};

// Totals live outside the activity so a game survives a trip to the home
// screen. They reset to 40 on boot.
int life1 = START_LIFE;
int life2 = START_LIFE;

// Seven-segment digit masks, bit 0..6 = a,b,c,d,e,f,g.
constexpr uint8_t SEGMENTS[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};

// A number rotated by 90 degrees and centered in a screen box.
// Local frame: u runs along the reading direction, v from glyph top to bottom.
struct RotatedText {
  const GfxRenderer& renderer;
  bool clockwise;
  int cx, cy;  // screen center of the text box
  int boxW;    // text length along u
  int boxH;    // text height along v

  void fill(int u, int v, int du, int dv) const {
    if (clockwise) {
      // u -> screen down, v -> screen left
      renderer.fillRect(cx + boxH / 2 - v - dv, cy - boxW / 2 + u, dv, du);
    } else {
      // u -> screen up, v -> screen right
      renderer.fillRect(cx - boxH / 2 + v, cy + boxW / 2 - u - du, dv, du);
    }
  }
};

// Draws one digit with its top edge at v0.
void drawDigit(const RotatedText& t, int digit, int u0, int v0, int w, int h, int s) {
  const uint8_t m = SEGMENTS[digit];
  const int half = h / 2;
  if (digit == 1) {
    t.fill(u0 + (w - s) / 2, v0, s, h);  // centered stroke reads better than b+c
    return;
  }
  if (m & 0x01) t.fill(u0, v0, w, s);                                    // a
  if (m & 0x02) t.fill(u0 + w - s, v0, s, half + s / 2);                 // b
  if (m & 0x04) t.fill(u0 + w - s, v0 + half - s / 2, s, h - half + s / 2);  // c
  if (m & 0x08) t.fill(u0, v0 + h - s, w, s);                            // d
  if (m & 0x10) t.fill(u0, v0 + half - s / 2, s, h - half + s / 2);      // e
  if (m & 0x20) t.fill(u0, v0, s, half + s / 2);                         // f
  if (m & 0x40) t.fill(u0, v0 + half - s / 2, w, s);                     // g
}

// Draws value centered in the screen rect (x,y,w,h), rotated, with the last
// change ("-3", "+2") in small digits below it when delta is not 0.
void drawLife(const GfxRenderer& renderer, int value, int delta, bool clockwise, int x, int y, int w, int h) {
  char text[4];
  const int n = snprintf(text, sizeof(text), "%d", value);
  constexpr int PAD = 28;
  // Screen height of the area is the text length, screen width its height.
  const int availLen = h - 2 * PAD;
  // Room below the number is always reserved so it never moves.
  constexpr int SMALL_H = 48;
  constexpr int SMALL_W = 26;
  constexpr int SMALL_STROKE = 6;
  constexpr int SMALL_GAP = 8;
  const int lineGap = 20;
  const int availHeight = w - 2 * PAD - lineGap - SMALL_H;
  const int gap = availLen / 12;
  // Size for at least two digits so the number does not jump at 9 -> 10.
  const int slots = std::max(n, 2);
  int digitW = (availLen - (slots - 1) * gap) / slots;
  digitW = std::min(digitW, availHeight / 2);
  const int digitH = std::min(availHeight, digitW * 2);
  const int stroke = std::max(6, digitW / 4);
  const int totalW = n * digitW + (n - 1) * gap;

  const int blockH = digitH + lineGap + SMALL_H;

  const RotatedText t{renderer, clockwise, x + w / 2, y + h / 2, totalW, blockH};
  for (int i = 0; i < n; ++i) {
    drawDigit(t, text[i] - '0', i * (digitW + gap), 0, digitW, digitH, stroke);
  }
  if (delta == 0) return;

  char deltaText[5];
  const int dn = snprintf(deltaText, sizeof(deltaText), "%d", std::abs(delta));
  const int signW = SMALL_W;
  const int deltaW = signW + dn * (SMALL_W + SMALL_GAP);
  const int v0 = digitH + lineGap;
  const RotatedText d{renderer, clockwise, x + w / 2, y + h / 2, deltaW, blockH};
  d.fill(0, v0 + (SMALL_H - SMALL_STROKE) / 2, signW, SMALL_STROKE);  // minus bar
  if (delta > 0) d.fill((signW - SMALL_STROKE) / 2, v0 + (SMALL_H - signW) / 2, SMALL_STROKE, signW);
  for (int i = 0; i < dn; ++i) {
    drawDigit(d, deltaText[i] - '0', signW + SMALL_GAP + i * (SMALL_W + SMALL_GAP), v0, SMALL_W, SMALL_H,
              SMALL_STROKE);
  }
}

void drawKeyCircle(const GfxRenderer& renderer, int cx, int cy, bool plus) {
  constexpr int D = 46;
  constexpr int BAR = 22;
  constexpr int THICK = 4;
  renderer.drawRoundedRect(cx - D / 2, cy - D / 2, D, D, 3, D / 2, true);
  renderer.fillRect(cx - BAR / 2, cy - THICK / 2, BAR, THICK);
  if (plus) renderer.fillRect(cx - THICK / 2, cy - BAR / 2, THICK, BAR);
}
}  // namespace

void LifePointsActivity::onEnter() {
  Activity::onEnter();
  {
    RenderLock lock;
    savedOrientation = renderer.getOrientation();
    renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  }
  // Keys still held from the menu must be released before they count.
  for (int i = 0; i < FRONT_KEYS; ++i) pressActive[i] = false;
  sideActive = false;
  fullRefreshPending = true;
  requestUpdate();
}

void LifePointsActivity::onExit() {
  // ActivityManager already holds the render lock here (exitActivity).
  renderer.setOrientation(savedOrientation);
  Activity::onExit();
}

// Full refresh only on open and on reset; every other change is a fast refresh.
void LifePointsActivity::markChanged(const bool forceFull) {
  if (forceFull) fullRefreshPending = true;
  requestUpdate();
}

void LifePointsActivity::applyPress(const int key, const bool longPress, const unsigned long now) {
  const int amount = longPress ? 5 : 1;
  const int before1 = life1;
  const int before2 = life2;
  switch (key) {
    // Keys 1-2 sit under the bottom player (life2), keys 3-4 serve the top one.
    case 0:
      life2 = std::max(0, life2 - amount);
      break;
    case 1:
      life2 = std::min(MAX_LIFE, life2 + amount);
      break;
    case 2:
      life1 = std::min(MAX_LIFE, life1 + amount);
      break;
    case 3:
      life1 = std::max(0, life1 - amount);
      break;
    default:
      return;
  }
  const int player = key <= 1 ? 1 : 0;
  const int change = player == 0 ? life1 - before1 : life2 - before2;
  if (change == 0) return;  // already at 0 or the maximum: nothing to redraw

  // Presses less than DELTA_DELAY_MS apart add up to one shown change.
  if (deltaPending[player]) {
    pendingDelta[player] += change;
  } else {
    pendingDelta[player] = change;
    deltaPending[player] = true;
  }
  shownDelta[player] = 0;  // hidden while the player is still pressing
  lastChange[player] = now;
  markChanged(false);
}

void LifePointsActivity::handleExitSequence(const int key, const bool longPress, const unsigned long now) {
  if (seqStep > 0 && now - seqStart > EXIT_WINDOW_MS) seqStep = 0;
  if (!longPress && seqStep > 0 && key == EXIT_SEQ[seqStep]) {
    ++seqStep;
  } else if (!longPress && key == EXIT_SEQ[0]) {
    // Any short key 1 (re)starts the sequence.
    seqStep = 1;
    seqStart = now;
    seqLife1 = life1;
    seqLife2 = life2;
  } else {
    seqStep = 0;
  }
}

void LifePointsActivity::loop() {
  const unsigned long now = millis();

  // Side keys sit on a separate ADC pin, so holding one is unambiguous.
  const bool sideDown = gpio.isPressed(HalGPIO::BTN_UP) || gpio.isPressed(HalGPIO::BTN_DOWN);
  if (sideDown && !sideActive) {
    sideActive = true;
    sideResetDone = false;
    sideStart = now;
  } else if (!sideDown) {
    sideActive = false;
  }
  if (sideActive && !sideResetDone && now - sideStart >= RESET_HOLD_MS) {
    sideResetDone = true;
    life1 = START_LIFE;
    life2 = START_LIFE;
    seqStep = 0;
    for (int p = 0; p < 2; ++p) {
      pendingDelta[p] = 0;
      deltaPending[p] = false;
      shownDelta[p] = 0;
    }
    markChanged(true);
  }

  // Show a player's summed change once they stopped pressing for 2 s. The
  // wait restarts on release and is paused while one of their keys is down,
  // so a hold (5) followed by short presses still adds up to one change.
  for (int p = 0; p < 2; ++p) {
    const bool keyDown = p == 1 ? (pressActive[0] || pressActive[1]) : (pressActive[2] || pressActive[3]);
    if (keyDown) continue;
    if (deltaPending[p] && now - lastChange[p] >= DELTA_DELAY_MS) {
      deltaPending[p] = false;
      if (pendingDelta[p] != 0) {
        shownDelta[p] = pendingDelta[p];
        requestUpdate();  // fast refresh
      }
    }
  }

  for (int key = 0; key < FRONT_KEYS; ++key) {
    if (gpio.wasPressed(FRONT_KEY[key])) {
      pressActive[key] = true;
      pressStart[key] = now;
    }
    if (gpio.wasReleased(FRONT_KEY[key]) && pressActive[key]) {
      pressActive[key] = false;
      const bool longPress = now - pressStart[key] >= LONG_PRESS_MS;
      // Apply first so every press gives feedback; the exit sequence undoes it.
      handleExitSequence(key, longPress, now);
      applyPress(key, longPress, now);
      if (seqStep == 4) {
        life1 = seqLife1;
        life2 = seqLife2;
        activityManager.goHome(HomeMenuItem::LIFEPOINTS);
        return;
      }
    }
  }
}

void LifePointsActivity::render(RenderLock&&) {
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  constexpr int MARGIN = 6;
  constexpr int LINE = 4;
  constexpr int KEY_STRIP = 72;  // bottom band for the +/- circles

  renderer.clearScreen();
  renderer.drawRect(MARGIN, MARGIN, width - 2 * MARGIN, height - 2 * MARGIN, LINE, true);
  const int mid = height / 2;
  renderer.fillRect(MARGIN, mid - LINE / 2, width - 2 * MARGIN, LINE);

  // Halves mirror each other around the divider; the bottom one stops above
  // the key strip, so both number areas are the same size.
  const int innerX = MARGIN + LINE;
  const int innerW = width - 2 * innerX;
  const int areaH = mid - LINE / 2 - innerX - KEY_STRIP;
  drawLife(renderer, life1, shownDelta[0], false, innerX, mid - LINE / 2 - areaH, innerW, areaH);
  drawLife(renderer, life2, shownDelta[1], true, innerX, mid + LINE / 2, innerW, areaH);

  // Circle centers match the theme's button-hint slots (BaseTheme::drawButtonHints).
  constexpr int narrowCenters[4] = {78, 183, 298, 403};
  constexpr int wideCenters[4] = {91, 207, 321, 437};
  const int* centers = width >= 528 ? wideCenters : narrowCenters;
  const int circleY = height - MARGIN - LINE - KEY_STRIP / 2;
  constexpr bool PLUS[4] = {false, true, true, false};
  for (int i = 0; i < 4; ++i) drawKeyCircle(renderer, centers[i], circleY, PLUS[i]);

  renderer.displayBuffer(fullRefreshPending.exchange(false) ? HalDisplay::FULL_REFRESH : HalDisplay::FAST_REFRESH);
}
