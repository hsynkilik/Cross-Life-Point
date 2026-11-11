#include "LifePointsActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalGPIO.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "CrossPointSettings.h"
#include "LifePointsArt.h"
#include "fontIds.h"

namespace {
constexpr int START_LIFE = 40;
constexpr int MAX_LIFE = 999;
constexpr unsigned long LONG_PRESS_MS = 600;
constexpr unsigned long HISTORY_HOLD_MS = 1000;  // top side key
constexpr unsigned long RESET_HOLD_MS = 2000;    // bottom side key
constexpr unsigned long IDLE_SLEEP_MS = 20UL * 60UL * 1000UL;
constexpr unsigned long EXIT_WINDOW_MS = 3000;
constexpr unsigned long DELTA_DELAY_MS = 2000;
constexpr unsigned long TROPHY_DELAY_MS = 2000;

// Screen layout (portrait): outer frame, divider, and the +/- band at each end.
constexpr int MARGIN = 6;
constexpr int LINE = 4;
constexpr int KEY_STRIP = 72;

// Physical front keys, left to right.
constexpr uint8_t FRONT_KEY[4] = {HalGPIO::BTN_BACK, HalGPIO::BTN_CONFIRM, HalGPIO::BTN_LEFT, HalGPIO::BTN_RIGHT};
// Exit sequence: keys 1,4,1,4 (0-based 0,3,0,3).
constexpr int EXIT_SEQ[4] = {0, 3, 0, 3};

// Totals live outside the activity so a game survives a trip to the home
// screen. They reset to 40 on boot.
int life1 = START_LIFE;
int life2 = START_LIFE;

// Per player (0 = top, 1 = bottom): how many changes this game, and the
// newest HISTORY_SHOWN of them in a ring. Cleared by reset.
constexpr int HISTORY_SHOWN = 9;
struct HistoryEntry {
  int16_t change;
  int16_t total;
};
int historyCount[2] = {};
HistoryEntry historyRing[2][HISTORY_SHOWN] = {};

void addHistory(const int player, const int change, const int total) {
  historyRing[player][historyCount[player] % HISTORY_SHOWN] = {static_cast<int16_t>(change),
                                                               static_cast<int16_t>(total)};
  ++historyCount[player];
}

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

// Caption under the game-over art: 7-row pixel font, CAPTION_PX per pixel.
constexpr int CAPTION_PX = 5;
constexpr int CAPTION_GAP = 20;

const lifepoints_art::Glyph* findGlyph(const char ch) {
  for (const auto& glyph : lifepoints_art::FONT) {
    if (glyph.ch == ch) return &glyph;
  }
  return nullptr;
}

int captionWidth(const char* text) {
  int width = 0;
  for (const char* p = text; *p; ++p) {
    const auto* glyph = findGlyph(*p);
    if (!glyph) continue;
    if (width > 0) width += CAPTION_PX;
    width += static_cast<int>(strlen(glyph->rows[0])) * CAPTION_PX;
  }
  return width;
}

// Skull or trophy (with an optional ray burst) and a caption below it,
// centered in the area (x,y,w,h) and rotated like the totals. Ray start and
// end are fractions of the art's width; 0 means no rays.
void drawGameOver(const GfxRenderer& renderer, const bool clockwise, const int x, const int y, const int w,
                  const int h, const char* const* grid, const int rows, const int cols, const int cell,
                  const float rayFrom, const float rayTo, const char* caption) {
  const int gridW = cols * cell;
  const int gridH = rows * cell;
  const bool rays = rayTo > 0.0f;
  const float rayStart = gridW * rayFrom;
  const float rayEnd = gridW * rayTo;
  const int side = rays ? static_cast<int>(2 * rayEnd) : gridW;
  const int artH = std::max(side, gridH);
  const int textW = captionWidth(caption);
  const int boxW = std::max({side, gridW, textW});
  const int boxH = artH + CAPTION_GAP + 7 * CAPTION_PX;
  const RotatedText t{renderer, clockwise, x + w / 2, y + h / 2, boxW, boxH};

  const int cu = boxW / 2;
  const int cv = artH / 2;
  const int u0 = cu - gridW / 2;
  const int v0 = cv - gridH / 2;
  for (int r = 0; r < rows; ++r) {
    for (int c = 0; c < cols; ++c) {
      if (grid[r][c] == '#') t.fill(u0 + c * cell, v0 + r * cell, cell, cell);
    }
  }

  if (rays) {
    // Eight dashed rays, one cell thick, every 45 degrees.
    for (int k = 0; k < 8; ++k) {
      const float a = k * static_cast<float>(M_PI) / 4.0f;
      const float cs = cosf(a);
      const float sn = sinf(a);
      for (float rr = rayStart; rr <= rayEnd; rr += cell * 0.7f) {
        t.fill(static_cast<int>(cu + cs * rr) - cell / 2, static_cast<int>(cv + sn * rr) - cell / 2, cell, cell);
      }
    }
  }

  int u = (boxW - textW) / 2;
  const int textV = artH + CAPTION_GAP;
  for (const char* p = caption; *p; ++p) {
    const auto* glyph = findGlyph(*p);
    if (!glyph) continue;
    const int glyphW = static_cast<int>(strlen(glyph->rows[0]));
    for (int r = 0; r < 7; ++r) {
      for (int c = 0; c < glyphW; ++c) {
        if (glyph->rows[r][c] == '#') t.fill(u + c * CAPTION_PX, textV + r * CAPTION_PX, CAPTION_PX, CAPTION_PX);
      }
    }
    u += glyphW * CAPTION_PX + CAPTION_PX;
  }
}

// One player's history in the current (landscape) orientation, starting at
// (x0, y0) with width w along the reading direction: "Start 40" while all
// entries fit, then the newest HISTORY_SHOWN entries, the newest in bold.
void drawHistory(const GfxRenderer& renderer, const int player, const int x0, const int y0, const int w) {
  constexpr int FONT = NOTOSANS_18_FONT_ID;
  constexpr int PAD = 18;
  constexpr int ROW = 38;
  const int lineH = renderer.getLineHeight(FONT);
  renderer.drawText(FONT, x0 + PAD, y0 + 16, "YOUR HISTORY", true, EpdFontFamily::BOLD);
  renderer.fillRect(x0 + PAD, y0 + 56, w - 2 * PAD, 2);

  const int changeRight = x0 + w / 2 + 10;
  const int totalRight = x0 + w - PAD;
  int y = y0 + 70;
  const auto row = [&](const char* label, const char* change, const char* total, const bool bold,
                       const bool arrow) {
    const auto style = bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
    renderer.drawText(FONT, x0 + PAD, y, label, true, style);
    renderer.drawText(FONT, changeRight - renderer.getTextWidth(FONT, change, style), y, change, true, style);
    const int totalW = renderer.getTextWidth(FONT, total, style);
    renderer.drawText(FONT, totalRight - totalW, y, total, true, style);
    if (arrow) {
      // Drawn as lines: the built-in fonts have no arrow glyph.
      const int ax = totalRight - totalW - 34;
      const int ay = y + lineH / 2;
      renderer.drawLine(ax, ay, ax + 22, ay, 2, true);
      renderer.drawLine(ax + 22, ay, ax + 15, ay - 6, 2, true);
      renderer.drawLine(ax + 22, ay, ax + 15, ay + 6, 2, true);
    }
    y += ROW;
  };

  const int count = historyCount[player];
  if (count <= HISTORY_SHOWN) row("Start", "", "40", false, false);
  for (int i = std::max(0, count - HISTORY_SHOWN); i < count; ++i) {
    const HistoryEntry& e = historyRing[player][i % HISTORY_SHOWN];
    char label[8];
    char change[8];
    char total[8];
    snprintf(label, sizeof(label), "%d.", i + 1);
    snprintf(change, sizeof(change), "%+d", e.change);
    snprintf(total, sizeof(total), "%d", e.total);
    row(label, change, total, i == count - 1, true);
  }
}

void drawKeyCircle(const GfxRenderer& renderer, int cx, int cy, bool plus) {
  constexpr int D = 46;
  constexpr int BAR = 22;
  constexpr int THICK = 4;
  renderer.drawRoundedRect(cx - D / 2, cy - D / 2, D, D, 3, D / 2, true);
  if (gpio.hasTouch()) {
    // Touch layout: players read the screen sideways, so the minus bar runs
    // top to bottom.
    renderer.fillRect(cx - THICK / 2, cy - BAR / 2, THICK, BAR);
    if (plus) renderer.fillRect(cx - BAR / 2, cy - THICK / 2, BAR, THICK);
    return;
  }
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
  touchZone = -1;
  for (int i = 0; i < 2; ++i) sideActive[i] = false;
  showHistory = false;
  lastInput = millis();
  // A game decided before leaving shows its trophy right away.
  gameOver = (life1 == 0) != (life2 == 0);
  trophyShown = gameOver;
  fullRefreshPending = true;
  requestUpdate();
}

// Exactly one player at 0 ends the game; the trophy follows TROPHY_DELAY_MS
// later. Both at 0 shows two skulls and no trophy.
void LifePointsActivity::updateGameOver(const unsigned long now) {
  const bool over = (life1 == 0) != (life2 == 0);
  if (over && !gameOver) {
    gameOverSince = now;
    trophyShown = false;
  } else if (!over) {
    trophyShown = false;
  }
  gameOver = over;
}

// Stay awake until IDLE_SLEEP_MS without input in LifePoints. The device's own
// sleep timer then adds its timeout, so the total is IDLE_SLEEP_MS. A longer
// setting (or "never") is left alone.
bool LifePointsActivity::preventAutoSleep() {
  const unsigned long global = SETTINGS.getSleepTimeoutMs();
  if (global == 0 || global >= IDLE_SLEEP_MS) return false;
  return millis() - lastInput < IDLE_SLEEP_MS - global;
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

// Touch boards: the +/- band at each end, split in halves. Returns the same
// key index as the X4 front keys (0 bottom -, 1 bottom +, 2 top +, 3 top -),
// or -1 outside the bands. Each player has - on their own left.
int LifePointsActivity::touchZoneAt(const int x, const int y) const {
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  const int band = MARGIN + LINE + KEY_STRIP;
  const bool leftHalf = x < width / 2;
  if (y >= height - band) return leftHalf ? 0 : 1;
  if (y < band) return leftHalf ? 2 : 3;
  return -1;
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
  updateGameOver(now);
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

  const bool touch = gpio.hasTouch();

  // Side keys, held: history toggles after 1 s, reset after 2 s.
  // X4: top / bottom side key (own ADC pin, so holding one is unambiguous).
  // X4 Pro (touch): Right nav button = history, Left = reset.
  const uint8_t SIDE_KEY[2] = {touch ? HalGPIO::BTN_RIGHT : HalGPIO::BTN_UP,
                               touch ? HalGPIO::BTN_LEFT : HalGPIO::BTN_DOWN};
  constexpr unsigned long SIDE_HOLD_MS[2] = {HISTORY_HOLD_MS, RESET_HOLD_MS};
  for (int s = 0; s < 2; ++s) {
    const bool down = gpio.isPressed(SIDE_KEY[s]);
    if (down) lastInput = now;
    if (down && !sideActive[s]) {
      sideActive[s] = true;
      sideDone[s] = false;
      sideStart[s] = now;
    } else if (!down) {
      sideActive[s] = false;
    }
    if (!sideActive[s] || sideDone[s] || now - sideStart[s] < SIDE_HOLD_MS[s]) continue;
    sideDone[s] = true;
    if (s == 0) {
      showHistory = !showHistory;
      requestUpdate();
      continue;
    }
    life1 = START_LIFE;
    life2 = START_LIFE;
    seqStep = 0;
    for (int p = 0; p < 2; ++p) {
      pendingDelta[p] = 0;
      deltaPending[p] = false;
      shownDelta[p] = 0;
      historyCount[p] = 0;
    }
    showHistory = false;
    updateGameOver(now);
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
        addHistory(p, pendingDelta[p], p == 0 ? life1 : life2);
        requestUpdate();  // fast refresh
      }
    }
  }

  if (gameOver && !trophyShown && now - gameOverSince >= TROPHY_DELAY_MS) {
    trophyShown = true;
    requestUpdate();  // fast refresh
  }

  if (touch) {
    // Touch zones reuse the key bookkeeping: hold time, change summing, and the
    // pause of the change indicator while a finger is down. Home leaves via
    // the ActivityManager's Home key handling.
    // Only the four +/- fields react, and a press counts only when the finger
    // lifts inside the field it started in: sliding out cancels it.
    int x = 0;
    int y = 0;
    if (mappedInput.wasScreenTouchPressed(x, y)) {
      touchZone = touchZoneAt(x, y);
      touchInZone = touchZone >= 0;
      if (touchZone >= 0) {
        pressActive[touchZone] = true;
        pressStart[touchZone] = now;
        lastInput = now;
      }
    }
    if (touchZone >= 0 && mappedInput.isScreenTouchHeld(x, y)) touchInZone = touchZoneAt(x, y) == touchZone;
    if (touchZone >= 0 && mappedInput.wasScreenTouchReleased()) {
      const int zone = touchZone;
      touchZone = -1;
      pressActive[zone] = false;
      lastInput = now;
      if (touchInZone && showHistory) {
        // A field tap closes the history without changing a total.
        showHistory = false;
        requestUpdate();
      } else if (touchInZone) {
        applyPress(zone, now - pressStart[zone] >= LONG_PRESS_MS, now);
      }
    }
    return;
  }

  for (int key = 0; key < FRONT_KEYS; ++key) {
    if (gpio.wasPressed(FRONT_KEY[key])) {
      pressActive[key] = true;
      pressStart[key] = now;
      lastInput = now;
    }
    if (gpio.wasReleased(FRONT_KEY[key]) && pressActive[key]) {
      pressActive[key] = false;
      lastInput = now;
      if (showHistory) {
        // Any front key closes the history without changing a total.
        showHistory = false;
        requestUpdate();
        continue;
      }
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
  renderer.clearScreen();
  renderer.drawRect(MARGIN, MARGIN, width - 2 * MARGIN, height - 2 * MARGIN, LINE, true);
  const int mid = height / 2;
  renderer.fillRect(MARGIN, mid - LINE / 2, width - 2 * MARGIN, LINE);

  // Halves mirror each other around the divider; the bottom one stops above
  // the key strip, so both number areas are the same size.
  const int innerX = MARGIN + LINE;
  const int innerW = width - 2 * innerX;
  const int areaH = mid - LINE / 2 - innerX - KEY_STRIP;
  const int areaY[2] = {mid - LINE / 2 - areaH, mid + LINE / 2};
  const int lives[2] = {life1, life2};
  const bool anyDead = life1 == 0 || life2 == 0;
  if (showHistory) {
    // Each half reads toward its player: draw the lists in landscape so the
    // normal UI font can be used, then return to portrait for the circles.
    // Top half = LandscapeClockwise, bottom half = LandscapeCounterClockwise.
    renderer.setOrientation(GfxRenderer::Orientation::LandscapeClockwise);
    drawHistory(renderer, 0, height - 1 - areaY[0] - areaH, innerX, areaH);
    renderer.setOrientation(GfxRenderer::Orientation::LandscapeCounterClockwise);
    drawHistory(renderer, 1, areaY[1], innerX, areaH);
    renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  }
  for (int p = 0; p < 2 && !showHistory; ++p) {
    const bool clockwise = p == 1;
    if (lives[p] == 0) {
      drawGameOver(renderer, clockwise, innerX, areaY[p], innerW, areaH, lifepoints_art::SKULL,
                   lifepoints_art::SKULL_ROWS, lifepoints_art::SKULL_COLS, 9, 0.0f, 0.0f, "YOU LOSE");
    } else if (anyDead && trophyShown) {
      drawGameOver(renderer, clockwise, innerX, areaY[p], innerW, areaH, lifepoints_art::TROPHY,
                   lifepoints_art::TROPHY_ROWS, lifepoints_art::TROPHY_COLS, 7, 0.66f, 0.8f, "YOU WIN");
    } else {
      // The change indicator stays hidden once someone is out.
      drawLife(renderer, lives[p], anyDead ? 0 : shownDelta[p], clockwise, innerX, areaY[p], innerW, areaH);
    }
  }

  const int bottomCircleY = height - MARGIN - LINE - KEY_STRIP / 2;
  if (gpio.hasTouch()) {
    // A frame around each band, split in the middle: the outlines are exactly
    // the touch zones (see touchZoneAt).
    constexpr int INSET = 6;
    constexpr int BOX_LINE = 3;
    const int boxX = MARGIN + LINE + INSET;
    const int boxW = width - 2 * boxX;
    const int boxH = KEY_STRIP - 2 * INSET;
    for (const int boxY : {MARGIN + LINE + INSET, height - MARGIN - LINE - KEY_STRIP + INSET}) {
      renderer.drawRect(boxX, boxY, boxW, boxH, BOX_LINE, true);
      renderer.fillRect(width / 2 - BOX_LINE / 2, boxY, BOX_LINE, boxH);
    }
    // One circle per touch zone: - on each player's left.
    const int topCircleY = MARGIN + LINE + KEY_STRIP / 2;
    drawKeyCircle(renderer, width / 4, topCircleY, true);
    drawKeyCircle(renderer, width * 3 / 4, topCircleY, false);
    drawKeyCircle(renderer, width / 4, bottomCircleY, false);
    drawKeyCircle(renderer, width * 3 / 4, bottomCircleY, true);
  } else {
    // Circle centers match the theme's button-hint slots (BaseTheme::drawButtonHints).
    constexpr int narrowCenters[4] = {78, 183, 298, 403};
    constexpr int wideCenters[4] = {91, 207, 321, 437};
    const int* centers = width >= 528 ? wideCenters : narrowCenters;
    constexpr bool PLUS[4] = {false, true, true, false};
    for (int i = 0; i < 4; ++i) drawKeyCircle(renderer, centers[i], bottomCircleY, PLUS[i]);
  }

  renderer.displayBuffer(fullRefreshPending.exchange(false) ? HalDisplay::FULL_REFRESH : HalDisplay::FAST_REFRESH);
}
