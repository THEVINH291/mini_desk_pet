/**
 * ============================================================================
 * MINI DESK PET (V3) — ESP32-C3 + ST7789 (240x240 IPS) + TTP223 TOUCH SENSOR
 * ============================================================================
 * Tính năng đồng bộ 1:1 với trọn bộ 3 video & hình thực tế:
 *  1. Khởi động (Boot): Chữ "Hi" trắng kèm Pet đầu tròn nghiêng ló lên chào
 *  2. Chạm nhẹ (< 0.5s): Đổi biểu cảm (Nháy mắt o <, Cau mày \ /, Cười tươi :D)
 *  3. Nhấn giữ lâu (>= 0.5s): Ép đầu nheo mắt > < rồi CHOÁNG (@ @ + sao quay quanh đầu)
 *  4. Tự diễn mỗi 15 giây không chạm (luân phiên đủ 3 hoạt ảnh):
 *      - Đánh bóng chày: Đội mũ vàng, vung gậy vàng đánh văng bóng Home Run
 *      - Bánh may mắn: Tách bánh rồi Pet nhảy tưng tưng ngay trên chữ "Luck is Yours"
 *      - Ngân nga nốt nhạc ♫ cùng đám mây bọt xốp bồng bềnh
 *  5. Tự ngủ sau 60 giây không tương tác:
 *      - Lim dim rồi tắt đen màn hình hoàn toàn để tiết kiệm điện
 *      - Chạm nhẹ để đánh thức đúng 3 pha: Mắt nhắm xanh ︶ ︶ + miệng ngáp nhỏ
 *        -> Mở 2 mắt tròn xanh ● ● trong bóng tối -> Bật sáng khung đầu tròn!
 *
 * CÀI ĐẶT TRONG ARDUINO IDE:
 *  - Library Manager: cài duy nhất thư viện "LovyanGFX" (by lovyang03)
 *  - Board: "ESP32C3 Dev Module"
 * ============================================================================
 */

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <math.h>

// ======================= CẤU HÌNH CHÂN KẾT NỐI =======================
static constexpr int PIN_TFT_SCLK  = 4;
static constexpr int PIN_TFT_MOSI  = 6;
static constexpr int PIN_TFT_RST   = 7;
static constexpr int PIN_TFT_DC    = 10;
static constexpr int PIN_TFT_CS    = 5;   // Bỏ trống nếu dùng màn ST7789 7 chân
static constexpr int PIN_TFT_BL    = -1;  // Nếu nối chân BLK vào GPIO (vd GPIO 0), điền số chân để tắt hẳn đèn nền khi ngủ

static constexpr int PIN_TOUCH     = 3;   // Cảm biến chạm TTP223 (I/O Active HIGH)
static constexpr int PIN_BUZZER    = 1;   // Còi thụ động Passive Buzzer (tùy chọn)
static constexpr bool ENABLE_SOUND = true;

static constexpr bool INVERT_DISPLAY     = true;
static constexpr uint8_t SCREEN_ROTATION = 0;

static constexpr float HOLD_DIZZY_SEC    = 0.50f; // Giữ >= 0.5s bị choáng
static constexpr float IDLE_INTERVAL_SEC = 15.0f; // Mỗi 15s tự diễn 1 hoạt ảnh
static constexpr float AUTO_SLEEP_SEC    = 60.0f; // Sau 60s không chạm -> tự ngủ tắt màn

// ======================= CẤU HÌNH DRIVER LOVYANGFX =======================
class LGFX_DeskPet : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 _panel_instance;
  lgfx::Bus_SPI      _bus_instance;

public:
  LGFX_DeskPet(void) {
    {
      auto cfg = _bus_instance.config();
      cfg.spi_host    = SPI2_HOST;
      cfg.spi_mode    = 3;
      cfg.freq_write  = 40000000;
      cfg.freq_read   = 16000000;
      cfg.spi_3wire   = true;
      cfg.use_lock    = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk    = PIN_TFT_SCLK;
      cfg.pin_mosi    = PIN_TFT_MOSI;
      cfg.pin_miso    = -1;
      cfg.pin_dc      = PIN_TFT_DC;
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }
    {
      auto cfg = _panel_instance.config();
      cfg.pin_cs          = PIN_TFT_CS;
      cfg.pin_rst         = PIN_TFT_RST;
      cfg.pin_busy        = -1;
      cfg.panel_width     = 240;
      cfg.panel_height    = 240;
      cfg.offset_x        = 0;
      cfg.offset_y        = 0;
      cfg.offset_rotation = 0;
      cfg.readable        = false;
      cfg.invert          = INVERT_DISPLAY;
      cfg.rgb_order       = false;
      cfg.dlen_16bit      = false;
      cfg.bus_shared      = false;
      _panel_instance.config(cfg);
    }
    setPanel(&_panel_instance);
  }
};

LGFX_DeskPet tft;
LGFX_Sprite  canvas(&tft);

// ======================= TRẠNG THÁI HOẠT ĐỘNG =======================
enum PetMode {
  MODE_BOOT = 0,     // Màn chào "Hi" khi mới cấp nguồn
  MODE_IDLE,         // Bình thường
  MODE_WINK,         // Nháy mắt (o <)
  MODE_ANGRY,        // Cau mày (\ /)
  MODE_HAPPY,        // Cười tươi (:D)
  MODE_DIZZY,        // Choáng váng khi giữ lâu (@ @ + sao quay quanh đầu)
  MODE_BASEBALL,     // Hoạt ảnh 15s #1: Đội mũ vàng đánh bóng chày Home Run
  MODE_FORTUNE,      // Hoạt ảnh 15s #2: Nhảy trên dòng chữ "Luck is Yours"
  MODE_HUMMING,      // Hoạt ảnh 15s #3: Ngân nga nốt nhạc ♫ + mây bọt
  MODE_FALL_ASLEEP,  // Chuyển sang lim dim rồi tắt màn
  MODE_SLEEP,        // Ngủ sâu (màn hình tối đen hoàn toàn)
  MODE_WAKEUP        // Thức dậy 3 giai đoạn khi chạm lúc đang ngủ
};

const PetMode TAP_SEQUENCE[] = {
  MODE_WINK, MODE_ANGRY, MODE_HAPPY, MODE_IDLE,
  MODE_ANGRY, MODE_WINK, MODE_HAPPY
};
const int TAP_SEQ_LEN = sizeof(TAP_SEQUENCE) / sizeof(TAP_SEQUENCE[0]);

const PetMode IDLE_STORIES[] = {
  MODE_BASEBALL, MODE_FORTUNE, MODE_HUMMING
};
const int IDLE_STORY_LEN = sizeof(IDLE_STORIES) / sizeof(IDLE_STORIES[0]);

int seqIndex   = 0;
int storyIndex = 0;

struct PetState {
  PetMode mode           = MODE_BOOT;
  float   animElapsed    = 0.0f;
  float   modeDuration   = 3.1f;
  float   idleCountdown  = IDLE_INTERVAL_SEC;
  float   noTouchElapsed = 0.0f;

  float   blink          = 1.0f;
  float   nextBlink      = 2.4f;
  bool    blinking       = false;
  float   blinkPhase     = 0.0f;

  float   gazeX          = 0.0f;
  float   gazeY          = 0.0f;
  float   targetGazeX    = 0.0f;
  float   targetGazeY    = 0.0f;
  float   nextGaze       = 1.8f;

  float   bounce         = 0.0f;
  float   angerMix       = 0.0f;
  float   happyMix       = 0.0f;
  float   winkMix        = 0.0f;

  bool    holding              = false;
  float   holdDuration         = 0.0f;
  bool    dizzyTriggeredInHold = false;
  bool    batHitPlayed         = false;
  bool    popPlayed            = false;
} pet;

unsigned long lastFrameMs = 0;

// ======================= BẢNG MÀU RGB565 =======================
uint16_t colBg;
uint16_t colHaloBlue;
uint16_t colSkyBlue;
uint16_t colSleepBlue;
uint16_t colBubbleFill;
uint16_t colIceBlue;
uint16_t colWhite;
uint16_t colYellow;
uint16_t colYellowLight;
uint16_t colGoldDark;
uint16_t colBannerBg;
uint16_t colBannerText;

void initPalette() {
  colBg          = tft.color565(0, 0, 0);
  colHaloBlue    = tft.color565(14, 58, 138);
  colSkyBlue     = tft.color565(36, 155, 255);
  colSleepBlue   = tft.color565(56, 189, 248);
  colBubbleFill  = tft.color565(14, 72, 128);
  colIceBlue     = tft.color565(135, 212, 255);
  colWhite       = tft.color565(255, 255, 255);
  colYellow      = tft.color565(250, 204, 21);
  colYellowLight = tft.color565(254, 240, 138);
  colGoldDark    = tft.color565(180, 115, 8);
  colBannerBg    = tft.color565(219, 234, 254);
  colBannerText  = tft.color565(30, 58, 138);
}

void setBacklight(bool on) {
  if (PIN_TFT_BL >= 0) {
    digitalWrite(PIN_TFT_BL, on ? HIGH : LOW);
  }
}

// ======================= HIỆU ỨNG ÂM THANH BUZZER =======================
void playChirp(PetMode m) {
  if (!ENABLE_SOUND || PIN_BUZZER < 0) return;
  switch (m) {
    case MODE_BOOT:
      tone(PIN_BUZZER, 659, 55); delay(55);
      tone(PIN_BUZZER, 987, 55); delay(55);
      tone(PIN_BUZZER, 1318, 80);
      break;
    case MODE_DIZZY:
      tone(PIN_BUZZER, 620, 55); delay(55);
      tone(PIN_BUZZER, 370, 55); delay(55);
      tone(PIN_BUZZER, 560, 55); delay(55);
      tone(PIN_BUZZER, 310, 75);
      break;
    case MODE_FALL_ASLEEP:
      tone(PIN_BUZZER, 587, 65); delay(65);
      tone(PIN_BUZZER, 392, 90);
      break;
    case MODE_WAKEUP:
      tone(PIN_BUZZER, 380, 45); delay(45);
      tone(PIN_BUZZER, 520, 55);
      break;
    case MODE_WINK:
      tone(PIN_BUZZER, 880, 45); delay(45);
      tone(PIN_BUZZER, 1480, 55);
      break;
    case MODE_ANGRY:
      tone(PIN_BUZZER, 340, 65); delay(65);
      tone(PIN_BUZZER, 210, 75);
      break;
    case MODE_HAPPY:
    case MODE_BASEBALL:
    case MODE_FORTUNE:
    case MODE_HUMMING:
      tone(PIN_BUZZER, 740, 45); delay(45);
      tone(PIN_BUZZER, 988, 45); delay(45);
      tone(PIN_BUZZER, 1174, 65);
      break;
    default:
      tone(PIN_BUZZER, 720, 35);
      break;
  }
}

void setPetMode(PetMode newMode, float durationSec = 3.0f, bool sound = true, bool resetNoTouch = false) {
  if (newMode != MODE_SLEEP) setBacklight(true);
  pet.mode          = newMode;
  pet.animElapsed   = 0.0f;
  pet.modeDuration  = durationSec;
  pet.idleCountdown = IDLE_INTERVAL_SEC;
  if (resetNoTouch) pet.noTouchElapsed = 0.0f;
  pet.bounce        = 1.0f;
  pet.batHitPlayed  = false;
  pet.popPlayed     = false;
  if (sound) playChirp(newMode);
}

// ======================= CÁC HÀM VẼ CƠ BẢN =======================
void drawMochiHead(float cx, float cy, float rx, float ryTop, float ryBot,
                   float scale = 1.0f, float tiltRad = 0.0f, float maxLocalY = 999.0f) {
  const int STEPS = 36;
  float ct = cosf(tiltRad);
  float st = sinf(tiltRad);

  float px[STEPS + 1];
  float py[STEPS + 1];
  bool  vis[STEPS + 1];

  for (int i = 0; i <= STEPS; i++) {
    float a  = (i * 2.0f * 3.1415926f) / STEPS;
    float sa = sinf(a);
    float ca = cosf(a);

    float ry     = ((ryTop + ryBot) * 0.5f - (ryTop - ryBot) * 0.5f * sa) * scale;
    float rHoriz = rx * (1.0f + 0.045f * sa) * scale;

    float lx = ca * rHoriz;
    float ly = sa * ry;

    vis[i] = (ly <= maxLocalY);
    px[i]  = cx + lx * ct - ly * st;
    py[i]  = cy + lx * st + ly * ct;
  }

  for (int i = 0; i < STEPS; i++) {
    if (vis[i] && vis[i + 1]) {
      canvas.drawWideLine(px[i], py[i], px[i + 1], py[i + 1], 13.0f * scale, colHaloBlue);
    }
  }
  for (int i = 0; i < STEPS; i++) {
    if (vis[i] && vis[i + 1]) {
      canvas.drawWideLine(px[i], py[i], px[i + 1], py[i + 1], 9.5f * scale, colSkyBlue);
    }
  }
  for (int i = 0; i < STEPS; i++) {
    if (vis[i] && vis[i + 1]) {
      canvas.drawWideLine(px[i], py[i], px[i + 1], py[i + 1], 2.5f * scale, colIceBlue);
    }
  }
}

void drawPillEye(int cx, int cy, int w, int h, uint16_t color) {
  if (h < 4) h = 4;
  int r = (w < h ? w : h) / 2;
  canvas.fillSmoothRoundRect(cx - w / 2, cy - h / 2, w, h, r, color);
}

// Cung mắt cười cong lên (⌒)
void drawUpwardArcEye(float headCx, float headCy, float localEx, float localEy,
                      float radius, float thickness, float tiltRad, uint16_t color) {
  float ct = cosf(tiltRad);
  float st = sinf(tiltRad);
  const int SEG = 8;
  float prevX = 0, prevY = 0;
  for (int i = 0; i <= SEG; i++) {
    float a  = 3.1415926f * (1.15f + 0.70f * ((float)i / SEG));
    float lx = localEx + cosf(a) * radius;
    float ly = localEy + sinf(a) * radius;
    float sx = headCx + lx * ct - ly * st;
    float sy = headCy + lx * st + ly * ct;
    if (i > 0) canvas.drawWideLine(prevX, prevY, sx, sy, thickness, color);
    prevX = sx; prevY = sy;
  }
}

// Cung mắt nhắm ngủ cong xuống (︶)
void drawDownwardSleepEye(float cx, float cy, float radius, float thickness, uint16_t color) {
  const int SEG = 8;
  float prevX = 0, prevY = 0;
  for (int i = 0; i <= SEG; i++) {
    float a  = 3.1415926f * (0.15f + 0.70f * ((float)i / SEG));
    float sx = cx + cosf(a) * radius;
    float sy = cy + sinf(a) * radius;
    if (i > 0) canvas.drawWideLine(prevX, prevY, sx, sy, thickness, color);
    prevX = sx; prevY = sy;
  }
}

void drawSmallStar(float cx, float cy, float r, uint16_t color) {
  canvas.drawWideLine(cx - r, cy, cx + r, cy, 2.5f, color);
  canvas.drawWideLine(cx, cy - r, cx, cy + r, 2.5f, color);
  float d = r * 0.65f;
  canvas.drawWideLine(cx - d, cy - d, cx + d, cy + d, 1.8f, color);
  canvas.drawWideLine(cx - d, cy + d, cx + d, cy - d, 1.8f, color);
}

// ======================= 1. MÀN CHÀO "Hi" KHI BẬT NGUỒN =======================
void drawBootHiScene(float el) {
  float pop = el / 0.50f;
  if (pop > 1.0f) pop = 1.0f;
  float p1 = pop - 1.0f;
  float easePop = 1.0f + 1.8f * p1 * p1 * p1 + 0.8f * p1 * p1;

  float exitMix = 0.0f;
  if (el > 2.45f) {
    exitMix = (el - 2.45f) / 0.65f;
    if (exitMix > 1.0f) exitMix = 1.0f;
  }
  float smoothExit = exitMix * exitMix * (3.0f - 2.0f * exitMix);

  if (el > 0.18f && smoothExit < 0.85f) {
    float slide = (el - 0.18f) / 0.35f;
    if (slide > 1.0f) slide = 1.0f;
    int hiY = 52 + (int)((1.0f - slide) * 12.0f);
    canvas.setFont(&fonts::FreeSansBold24pt7b);
    canvas.setTextColor(colWhite);
    canvas.setCursor(40, hiY);
    canvas.print("Hi");
  }

  float wiggle = sinf(el * 7.0f) * 0.05f * (1.0f - smoothExit);
  float tilt   = (-0.50f + wiggle) * (1.0f - smoothExit);

  float startX = 142.0f;
  float startY = 152.0f + (1.0f - easePop) * 65.0f;
  float cx     = startX * (1.0f - smoothExit) + 120.0f * smoothExit;
  float cy     = startY * (1.0f - smoothExit) + 122.0f * smoothExit;

  if (smoothExit < 0.85f) {
    float clipBottom = 14.0f + smoothExit * 55.0f;
    drawMochiHead(cx, cy, 62.0f, 56.0f, 46.0f, 1.0f, tilt, clipBottom);
    drawUpwardArcEye(cx, cy, -22.0f, -4.0f, 10.0f, 4.2f, tilt, colWhite);
    drawUpwardArcEye(cx, cy,  22.0f, -4.0f, 10.0f, 4.2f, tilt, colWhite);
  } else {
    drawMochiHead(cx, cy, 64.0f, 56.0f, 46.0f, 1.0f, 0.0f, 999.0f);
    drawPillEye((int)cx - 24, (int)cy - 2, 13, 24, colWhite);
    drawPillEye((int)cx + 24, (int)cy - 2, 13, 24, colWhite);
  }
}

// ======================= 2. BỊ CHOÁNG KHI GIỮ LÂU =======================
void drawDizzyScene(float t) {
  float swayX = sinf(t * 12.0f) * 7.0f;
  float swayY = cosf(t * 9.0f)  * 3.0f;
  float tilt  = sinf(t * 10.0f) * 0.14f;

  float cx = 120.0f + swayX;
  float cy = 128.0f + swayY;

  drawMochiHead(cx, cy, 64.0f, 55.0f, 46.0f, 1.0f, tilt);

  float ct = cosf(tilt);
  float st = sinf(tilt);
  const float eyeOffsets[2] = {-25.0f, 25.0f};

  for (int idx = 0; idx < 2; idx++) {
    float ex   = cx + eyeOffsets[idx] * ct;
    float ey   = cy + eyeOffsets[idx] * st;
    float spin = t * (idx == 0 ? 8.5f : -8.5f);

    float prevX = 0, prevY = 0;
    for (int s = 0; s <= 22; s++) {
      float a  = s * 0.52f;
      float r  = 1.6f + a * 1.40f;
      float px = ex + cosf(a + spin) * r;
      float py = ey + sinf(a + spin) * r;
      if (s > 0) canvas.drawWideLine(prevX, prevY, px, py, 2.8f, colWhite);
      prevX = px; prevY = py;
    }
  }

  canvas.drawWideLine(cx - 12, cy + 22, cx - 4,  cy + 18, 2.8f, colIceBlue);
  canvas.drawWideLine(cx - 4,  cy + 18, cx + 4,  cy + 25, 2.8f, colIceBlue);
  canvas.drawWideLine(cx + 4,  cy + 25, cx + 12, cy + 21, 2.8f, colIceBlue);

  float orbitCx = 120.0f + swayX * 0.6f;
  float orbitCy = 56.0f;
  canvas.drawEllipse((int)orbitCx, (int)orbitCy, 46, 11, colGoldDark);

  for (int i = 0; i < 3; i++) {
    float ang = t * 5.2f + i * 2.09439f;
    float sx  = orbitCx + cosf(ang) * 46.0f;
    float sy  = orbitCy + sinf(ang) * 11.0f;
    float sr  = 5.0f + (sinf(ang) + 1.0f) * 1.8f;
    drawSmallStar(sx, sy, sr, colYellow);
  }
}

// ======================= 3. ĐÁNH BÓNG CHÀY (VIDEO ĐẦU TIÊN) =======================
void drawYellowBaseballCap(int cx, int topY, float scale) {
  int r = (int)(30.0f * scale);
  // Vòm mũ lưỡi trai màu vàng trên đỉnh đầu
  canvas.fillArc(cx, topY + (int)(5.0f * scale), r + 1, 0, 180, 360, colGoldDark);
  canvas.fillArc(cx, topY + (int)(5.0f * scale), r,     0, 180, 360, colYellow);
  // Nút chóp mũ
  canvas.fillSmoothCircle(cx, topY - r + (int)(4.0f * scale), (int)(3.5f * scale), colYellowLight);
  // Vành lưỡi trai chìa sang phải
  canvas.fillEllipse(cx + (int)(17.0f * scale), topY + (int)(5.0f * scale),
                     (int)(22.0f * scale), (int)(5.5f * scale), colYellow);
  canvas.drawEllipse(cx + (int)(17.0f * scale), topY + (int)(5.0f * scale),
                     (int)(22.0f * scale), (int)(5.5f * scale), colGoldDark);
}

void drawBaseballScene(float el, float t) {
  if (el < 1.15f) {
    // Pha 1 (00:00-00:01): Đội mũ vàng, giơ gậy bóng chày vàng bên trái chờ bóng tới
    float cx = 128.0f, cy = 144.0f, sc = 0.68f;
    drawMochiHead(cx, cy, 64.0f, 56.0f, 46.0f, sc);
    drawYellowBaseballCap((int)cx, (int)(cy - 36.0f), sc);

    drawPillEye((int)cx - 14, (int)cy - 2, 8, 16, colWhite);
    drawPillEye((int)cx + 18, (int)cy - 2, 8, 16, colWhite);

    // Gậy bóng chày vàng nghiêng sẵn sàng vung
    float waggle = sinf(t * 14.0f) * 0.08f;
    float batAng = -0.55f + waggle;
    float bx0 = cx - 46.0f;
    float by0 = cy + 10.0f;
    float bx1 = bx0 + sinf(batAng) * 52.0f;
    float by1 = by0 - cosf(batAng) * 52.0f;
    canvas.drawWideLine(bx0, by0, bx1, by1, 12.0f, colGoldDark);
    canvas.drawWideLine(bx0, by0, bx1, by1, 9.5f,  colYellow);
    canvas.fillSmoothCircle((int)bx0, (int)by0, 5, colYellowLight);

    // Quả bóng chày trắng bay lao vào từ bên phải
    if (el > 0.50f) {
      float p = (el - 0.50f) / 0.65f;
      int ballX = (int)(228.0f - p * 116.0f);
      int ballY = (int)(92.0f + p * 46.0f - sinf(p * 3.14159f) * 16.0f);
      canvas.fillSmoothCircle(ballX, ballY, 8, colWhite);
    }
  } else if (el < 1.62f) {
    // Pha 2 (00:02): Vung gậy vàng tạo vệt sáng ngang + đánh văng bóng!
    if (!pet.batHitPlayed) {
      pet.batHitPlayed = true;
      if (ENABLE_SOUND && PIN_BUZZER >= 0) {
        tone(PIN_BUZZER, 980, 35); delay(35);
        tone(PIN_BUZZER, 1760, 65);
      }
    }
    float sw = (el - 1.15f) / 0.47f;
    float cx = 122.0f, cy = 144.0f, sc = 0.68f;

    drawMochiHead(cx, cy, 68.0f, 52.0f, 46.0f, sc);
    drawYellowBaseballCap((int)cx, (int)(cy - 34.0f), sc);

    // Mắt nheo > < dồn lực vung gậy
    canvas.drawWideLine(cx - 20, cy - 6, cx - 11, cy,     3.6f, colWhite);
    canvas.drawWideLine(cx - 11, cy,     cx - 20, cy + 6, 3.6f, colWhite);
    canvas.drawWideLine(cx + 20, cy - 6, cx + 11, cy,     3.6f, colWhite);
    canvas.drawWideLine(cx + 11, cy,     cx + 20, cy + 6, 3.6f, colWhite);

    // Vệt vung gậy vàng rực quét ngang phía trước mặt
    int sweepEnd = 175 - (int)(sw * 165.0f);
    if (sweepEnd < 10) sweepEnd = 10;
    canvas.fillArc((int)cx, (int)cy + 4, 66, 48, sweepEnd, 175, colYellow);

    if (sw < 0.45f) {
      drawSmallStar(112.0f, 138.0f, 14.0f, colYellowLight);
    }
    int hitX = (int)(112.0f + sw * 65.0f);
    int hitY = (int)(138.0f - sw * 68.0f);
    canvas.fillSmoothCircle(hitX, hitY, 8, colWhite);
  } else {
    // Pha 3 (00:03-00:04): Home Run! Bóng bay vút lên trời lóe sao, Pet đội mũ vàng nhảy mừng!
    float post = (el - 1.62f) / 2.28f;
    float hop  = fabsf(sinf((el - 1.62f) * 8.5f)) * 8.0f;
    float cx = 120.0f, cy = 144.0f - hop, sc = 0.72f;

    drawMochiHead(cx, cy, 64.0f, 56.0f, 46.0f, sc);
    drawYellowBaseballCap((int)cx, (int)(cy - 38.0f), sc);
    drawUpwardArcEye(cx, cy, -16.0f, 2.0f, 8.5f, 3.8f, 0.0f, colWhite);
    drawUpwardArcEye(cx, cy,  16.0f, 2.0f, 8.5f, 3.8f, 0.0f, colWhite);

    if (post < 0.45f) {
      float bp = post / 0.45f;
      int bx = (int)(177.0f + bp * 34.0f);
      int by = (int)(70.0f - bp * 32.0f);
      canvas.drawWideLine(bx - 16, by + 15, bx, by, 3.0f, colIceBlue);
      int br = (int)(7.0f * (1.0f - bp * 0.65f));
      if (br < 2) br = 2;
      canvas.fillSmoothCircle(bx, by, br, colWhite);
    } else {
      float tw = 1.0f - (post - 0.45f) / 0.50f;
      if (tw > 0.05f) drawSmallStar(208.0f, 38.0f, 11.0f * tw, colYellow);
    }
  }
}

// ======================= 4. BÁNH MAY MẮN: NHẢY TRÊN CHỮ "Luck is Yours" =======================
void drawLuckBanner(int bannerY, int bw) {
  int bx = 120 - bw / 2;
  canvas.fillSmoothRoundRect(bx, bannerY, bw, 36, 9, colBannerBg);
  canvas.drawRoundRect(bx, bannerY, bw, 36, 9, colSkyBlue);
  if (bw >= 110) {
    canvas.setFont(&fonts::FreeSansBold9pt7b);
    canvas.setTextColor(colBannerText);
    int tw = canvas.textWidth("Luck is Yours");
    canvas.setCursor(120 - tw / 2, bannerY + 11);
    canvas.print("Luck is Yours");
  }
}

void drawFortuneScene(float el, float t) {
  if (el < 1.10f) {
    float shake = sinf(t * 36.0f) * 7.0f;
    int cx = 120 + (int)shake;
    int cy = 128;

    canvas.drawArc(cx - 42, cy - 6, 16, 14, 145, 215, colWhite);
    canvas.drawArc(cx - 50, cy - 6, 20, 18, 150, 210, colWhite);
    canvas.drawArc(cx + 42, cy - 6, 16, 14, 325, 35,  colWhite);
    canvas.drawArc(cx + 50, cy - 6, 20, 18, 330, 30,  colWhite);

    canvas.fillSmoothCircle(cx, cy + 4, 44, colYellow);
    canvas.fillTriangle(cx - 32, cy + 50, cx + 32, cy + 50, cx, cy + 2, colBg);
    canvas.drawWideLine(cx - 14, cy + 26, cx, cy - 6, 4.0f, colGoldDark);
    canvas.drawWideLine(cx + 14, cy + 26, cx, cy - 6, 4.0f, colGoldDark);
  } else if (el < 2.25f) {
    float openP = (el - 1.10f) / 0.35f;
    if (openP > 1.0f) openP = 1.0f;
    int splitX  = 22 + (int)(openP * 54.0f);
    int bannerY = 122 + (int)(openP * 24.0f);

    drawMochiHead(154.0f, (float)(bannerY - 20), 64.0f, 56.0f, 46.0f, 0.36f);
    drawPillEye(146, bannerY - 21, 4, 8, colWhite);
    drawPillEye(162, bannerY - 21, 4, 8, colWhite);

    canvas.fillArc(120 - splitX, bannerY + 18, 24, 4, 110, 250, colYellow);
    canvas.fillArc(120 + splitX, bannerY + 18, 24, 4, 290, 70,  colYellow);

    int bw = (int)(148.0f * openP);
    if (bw > 24) drawLuckBanner(bannerY, bw);
  } else {
    // Pha 3: Pet nhảy tưng tưng NGAY TRÊN DÒNG CHỮ "Luck is Yours"!
    float jumpPhase    = (el - 2.25f) * 8.2f;
    float hopSin       = fabsf(sinf(jumpPhase));
    float hopHeight    = hopSin * 28.0f;
    float landingPress = (hopSin < 0.22f) ? (1.0f - hopSin / 0.22f) : 0.0f;

    int bannerY = 148 + (int)(landingPress * 3.5f);

    // Hai đầu bánh quy vàng kèm nguyên vẹn dòng chữ "Luck is Yours"
    canvas.fillArc(120 - 78, bannerY + 18, 20, 3, 110, 250, colYellow);
    canvas.fillArc(120 + 78, bannerY + 18, 20, 3, 290, 70,  colYellow);
    drawLuckBanner(bannerY, 148);

    // Pet nhảy chạm đúng mép trên băng rôn "Luck is Yours" có độ nhún đàn hồi
    float petScale        = 0.46f;
    float sx              = 1.0f + landingPress * 0.14f - hopSin * 0.05f;
    float sy              = 1.0f - landingPress * 0.14f + hopSin * 0.06f;
    float petBottomRadius = 46.0f * petScale * sy + 4.0f;
    float petCy           = (float)bannerY - petBottomRadius - hopHeight;

    drawMochiHead(120.0f, petCy, 64.0f * sx, 56.0f * sy, 46.0f * sy, petScale);

    if (hopSin > 0.55f) {
      drawUpwardArcEye(120.0f, petCy, -11.0f, 3.0f, 5.5f, 3.0f, 0.0f, colWhite);
      drawUpwardArcEye(120.0f, petCy,  11.0f, 3.0f, 5.5f, 3.0f, 0.0f, colWhite);
    } else {
      drawPillEye(109, (int)petCy - 1, 5, (int)(10.0f * sy), colWhite);
      drawPillEye(131, (int)petCy - 1, 5, (int)(10.0f * sy), colWhite);
    }

    drawSmallStar(62.0f,  86.0f - hopSin * 4.0f, 6.0f, colYellow);
    drawSmallStar(178.0f, 92.0f - hopSin * 4.0f, 6.0f, colSkyBlue);
  }
}

// ======================= 5. NGÂN NGA NỐT NHẠC ♫ =======================
void drawHummingScene(float el, float t) {
  float side = (el < 1.6f) ? -1.0f : ((el < 2.9f) ? 1.0f : -0.4f);
  float sway = sinf(t * 8.0f) * 5.0f;
  float cx   = 120.0f - side * 12.0f + sway * 0.4f;
  float cy   = 128.0f;

  drawMochiHead(cx, cy, 64.0f, 56.0f, 46.0f, 0.72f);
  drawUpwardArcEye(cx, cy, -17.0f, 2.0f, 8.5f, 3.8f, 0.0f, colWhite);
  drawUpwardArcEye(cx, cy,  17.0f, 2.0f, 8.5f, 3.8f, 0.0f, colWhite);

  float puffX = cx + side * 38.0f + sinf(t * 18.0f) * 4.0f;
  float puffY = cy + 8.0f + cosf(t * 14.0f) * 3.0f;

  canvas.drawWideLine(cx + side * 8.0f, cy + 12.0f, puffX, puffY, 4.5f, colWhite);

  const int offsets[5][3] = {{0, 0, 15}, {-10, -4, 11}, {10, -3, 11}, {-6, 7, 10}, {8, 6, 10}};
  for (int i = 0; i < 5; i++) {
    canvas.fillSmoothCircle((int)puffX + offsets[i][0], (int)puffY + offsets[i][1], offsets[i][2] + 2, colSkyBlue);
  }
  for (int i = 0; i < 5; i++) {
    canvas.fillSmoothCircle((int)puffX + offsets[i][0], (int)puffY + offsets[i][1], offsets[i][2] - 2, colWhite);
  }

  int noteX     = (int)(cx - side * 46.0f);
  int noteFloat = ((int)(t * 22.0f)) % 24;
  int ny        = (int)cy - 22 - noteFloat;

  canvas.fillSmoothCircle(noteX - 6, ny + 8, 4, colSkyBlue);
  canvas.fillSmoothCircle(noteX + 6, ny + 5, 4, colSkyBlue);
  canvas.drawWideLine(noteX - 3, ny + 8, noteX - 3, ny - 6, 2.5f, colSkyBlue);
  canvas.drawWideLine(noteX - 3, ny - 6, noteX + 9, ny - 9, 2.5f, colSkyBlue);
  canvas.drawWideLine(noteX + 9, ny - 9, noteX + 9, ny + 5, 2.5f, colSkyBlue);

  canvas.fillRect((int)(puffX + side * 22.0f), (int)puffY - 22, 3, 3, colWhite);
  canvas.fillRect((int)(cx - side * 24.0f),    (int)cy - 48,    3, 3, colWhite);
}

// ======================= 6. TỰ NGỦ & THỨC DẬY (BONG BÓNG BỂ + NGÁI NGỦ) =======================
void drawGlossyBubble(int bx, int by, int r) {
  if (r < 2) return;
  canvas.fillSmoothCircle(bx, by, r, colBubbleFill);
  canvas.drawCircle(bx, by, r, colIceBlue);
  if (r > 3) canvas.drawCircle(bx, by, r - 1, colSleepBlue);
  if (r > 7) {
    int hr = (r * 68) / 100;
    canvas.drawArc(bx, by, hr, hr - 2, 200, 260, colWhite);
  }
}

void drawClosedSleepEyes() {
  drawDownwardSleepEye(96.0f,  112.0f, 10.0f, 4.0f, colSleepBlue);
  drawDownwardSleepEye(144.0f, 112.0f, 10.0f, 4.0f, colSleepBlue);
}

void drawFallAsleepScene(float el) {
  if (el < 0.65f) {
    drawMochiHead(120.0f, 122.0f, 64.0f, 56.0f, 46.0f, 1.0f);
  }
  if (el < 2.15f) {
    drawClosedSleepEyes();
    float p = el / 2.15f;
    int r = 7 + (int)(sinf(p * 3.14159f) * 11.0f);
    drawGlossyBubble(110 - r / 3, 134 - r / 6, r);
  }
  // Sau 2.15s màn hình chuyển sang đen hoàn toàn rồi vào MODE_SLEEP
}

void drawWakeUpScene(float el) {
  if (el < 0.75f) {
    // Giai đoạn 1: Đang ngủ thổi bong bóng căng phồng giống kẹo cao su sắp bể!
    float p = el / 0.75f;
    float wobble = sinf(el * 28.0f) * (p * 1.5f);
    drawClosedSleepEyes();

    int r  = 9 + (int)(p * 16.0f);
    int bx = (int)(113.0f - p * 8.0f + wobble);
    int by = (int)(133.0f - p * 4.0f - wobble * 0.5f);
    drawGlossyBubble(bx, by, r);
  } else if (el < 1.12f) {
    // Giai đoạn 2: BỂ CÁI BỤP! Tia nước bắn tung tóe + giật mình nheo mắt > <
    if (!pet.popPlayed) {
      pet.popPlayed = true;
      if (ENABLE_SOUND && PIN_BUZZER >= 0) {
        tone(PIN_BUZZER, 360, 25); delay(25);
        tone(PIN_BUZZER, 1520, 45);
      }
    }
    float popP = (el - 0.75f) / 0.37f;
    int bx = 105, by = 129;

    canvas.drawCircle(bx, by, 22 + (int)(popP * 18.0f), colIceBlue);
    for (int i = 0; i < 8; i++) {
      float ang  = (i * 2.0f * 3.14159f) / 8.0f + 0.3f;
      float dist = 18.0f + popP * 30.0f;
      int dx = bx + (int)(cosf(ang) * dist);
      int dy = by + (int)(sinf(ang) * dist);
      int dr = (int)(3.8f * (1.0f - popP * 0.7f));
      if (dr < 1) dr = 1;
      canvas.fillSmoothCircle(dx, dy, dr, colIceBlue);
    }

    int joltY = (int)(sinf(popP * 3.14159f) * -5.0f);
    canvas.drawWideLine(89,  108 + joltY, 101, 114 + joltY, 4.0f, colSleepBlue);
    canvas.drawWideLine(101, 114 + joltY, 89,  120 + joltY, 4.0f, colSleepBlue);
    canvas.drawWideLine(151, 108 + joltY, 139, 114 + joltY, 4.0f, colSleepBlue);
    canvas.drawWideLine(139, 114 + joltY, 151, 120 + joltY, 4.0f, colSleepBlue);
  } else if (el < 2.55f) {
    // Giai đoạn 3: NGÁI NGỦ! Hai mắt sụp mí lờ đờ chớp nặng trĩu + ngáp dài
    float gEl   = el - 1.12f;
    float swayX = sinf(gEl * 4.8f) * 4.5f;
    float swayY = cosf(gEl * 3.5f) * 2.0f;

    float d = (gEl - 0.58f) / 0.18f;
    float heavyBlink = expf(-(d * d));
    float lidDrop = 0.48f + heavyBlink * 0.42f;

    int eyeY = 117 + (int)swayY;
    const int baseEx[2] = {96 + (int)swayX, 144 + (int)swayX};

    for (int idx = 0; idx < 2; idx++) {
      int ex = baseEx[idx];
      canvas.fillSmoothCircle(ex, eyeY, 11, colSleepBlue);

      // Che phần trên của tròng mắt để tạo mí mắt sụp ngái ngủ
      int lidY = eyeY - 11 + (int)(lidDrop * 21.0f);
      int maskH = lidY - (eyeY - 13);
      if (maskH > 0) {
        canvas.fillRect(ex - 13, eyeY - 13, 26, maskH, colBg);
      }
      int slant = (idx == 0) ? -2 : 2;
      canvas.drawWideLine(ex - 12, lidY - slant, ex + 12, lidY + slant, 3.4f, colIceBlue);
    }

    // Miệng ngáp tròn o -> O -> o
    float yp = (gEl / 1.25f);
    if (yp < 1.0f) {
      float yawnSin = sinf(yp * 3.14159f);
      if (yawnSin > 0.08f) {
        int yRx = 4 + (int)(yawnSin * 3.5f);
        int yRy = 2 + (int)(yawnSin * 6.5f);
        canvas.fillEllipse(120 + (int)swayX, 139 + (int)swayY, yRx, yRy, colSleepBlue);
      }
    }
  } else {
    // Giai đoạn 4: Lắc nhẹ tỉnh ngủ -> bật sáng khung đầu tròn Mochi + mắt trắng sắc nét!
    float p = (el - 2.55f) / 0.55f;
    if (p > 1.0f) p = 1.0f;
    float shakeX = sinf(p * 3.14159f * 5.0f) * 4.0f * (1.0f - p);

    drawMochiHead(120.0f + shakeX, 122.0f, 64.0f, 56.0f, 46.0f, 0.92f + 0.08f * p);
    uint16_t eyeCol = (p > 0.45f) ? colWhite : colIceBlue;
    int eh = 18 + (int)(7.0f * p);
    drawPillEye(96  + (int)shakeX, 120, 13, eh, eyeCol);
    drawPillEye(144 + (int)shakeX, 120, 13, eh, eyeCol);
  }
}

// ======================= 7. KHUÔN MẶT CHÍNH =======================
void drawMainPetFace(float t) {
  float holdPress = pet.holding ? (pet.holdDuration / HOLD_DIZZY_SEC) : 0.0f;
  if (holdPress > 1.0f) holdPress = 1.0f;
  float shakeX = (holdPress > 0.15f) ? sinf(t * 45.0f) * (holdPress * 4.0f) : 0.0f;

  float bounceScaleX = 1.0f + sinf(pet.bounce * 3.14159f) * 0.06f + holdPress * 0.08f;
  float bounceScaleY = 1.0f - sinf(pet.bounce * 3.14159f) * 0.05f - pet.angerMix * 0.04f - holdPress * 0.09f;
  float bobY = sinf(t * 2.5f) * 2.2f + (pet.happyMix * sinf(t * 9.0f) * 2.5f) + holdPress * 5.0f;

  float cx = 120.0f + shakeX;
  float cy = 122.0f + bobY;

  drawMochiHead(cx, cy, 64.0f * bounceScaleX, 56.0f * bounceScaleY, 46.0f * bounceScaleY);

  int eyeOffsetX = 24;
  int gx  = (int)(pet.gazeX * (1.0f - pet.happyMix));
  int gy  = (int)(pet.gazeY * (1.0f - pet.happyMix));
  int icx = (int)cx;
  int icy = (int)cy;

  if (holdPress > 0.20f) {
    canvas.drawWideLine(icx - eyeOffsetX - 7, icy - 9, icx - eyeOffsetX + 7, icy,     4.8f, colWhite);
    canvas.drawWideLine(icx - eyeOffsetX + 7, icy,     icx - eyeOffsetX - 7, icy + 9, 4.8f, colWhite);
    canvas.drawWideLine(icx + eyeOffsetX + 7, icy - 9, icx + eyeOffsetX - 7, icy,     4.8f, colWhite);
    canvas.drawWideLine(icx + eyeOffsetX - 7, icy,     icx + eyeOffsetX + 7, icy + 9, 4.8f, colWhite);
  } else if (pet.angerMix > 0.45f) {
    int lx = icx - eyeOffsetX + gx / 2;
    int rx = icx + eyeOffsetX + gx / 2;
    int ey = icy + 3 + gy / 2;

    canvas.fillSmoothCircle(lx, ey, 13, colWhite);
    canvas.fillTriangle(lx - 16, ey - 16, lx + 16, ey - 16, lx + 16, ey + 4, colBg);

    canvas.fillSmoothCircle(rx, ey, 13, colWhite);
    canvas.fillTriangle(rx - 16, ey - 16, rx + 16, ey - 16, rx - 16, ey + 4, colBg);
  } else if (pet.happyMix > 0.45f) {
    int eyeH = (int)(15.0f * pet.blink);
    drawPillEye(icx - 22, icy - 13, 10, eyeH, colWhite);
    drawPillEye(icx + 22, icy - 13, 10, eyeH, colWhite);
    canvas.fillArc(icx, icy - 2, 26, 20, 28, 152, colWhite);
  } else if (pet.blinking && pet.blink < 0.38f && pet.winkMix < 0.3f) {
    drawUpwardArcEye(cx + gx, cy + gy, -eyeOffsetX, 4.0f, 10.0f, 3.8f, 0.0f, colWhite);
    drawUpwardArcEye(cx + gx, cy + gy,  eyeOffsetX, 4.0f, 10.0f, 3.8f, 0.0f, colWhite);
  } else {
    int eyeW = 13;
    int eyeH = (int)(25.0f * pet.blink);
    drawPillEye(icx - eyeOffsetX + gx, icy - 2 + gy, eyeW, eyeH, colWhite);

    if (pet.winkMix > 0.4f) {
      int rx = icx + eyeOffsetX + gx;
      int ry = icy - 2 + gy;
      canvas.drawWideLine(rx + 7, ry - 10, rx - 8, ry,      5.2f, colWhite);
      canvas.drawWideLine(rx - 8, ry,      rx + 7, ry + 10, 5.2f, colWhite);
    } else {
      drawPillEye(icx + eyeOffsetX + gx, icy - 2 + gy, eyeW, eyeH, colWhite);
    }
  }
}

// ======================= ĐỌC CẢM BIẾN CHẠM & ĐÁNH THỨC =======================
void updateTouch(float dt) {
  bool rawTouch = (digitalRead(PIN_TOUCH) == HIGH);

  if (rawTouch && !pet.holding) {
    pet.noTouchElapsed = 0.0f;
    // Nếu đang ngủ tắt màn -> đánh thức với hiệu ứng bể bong bóng + ngái ngủ!
    if (pet.mode == MODE_SLEEP || pet.mode == MODE_FALL_ASLEEP) {
      setPetMode(MODE_WAKEUP, 3.15f, true, true);
      // Đợi nhả tay để không tính nhầm thành giữ choáng
      while (digitalRead(PIN_TOUCH) == HIGH) { delay(10); }
      lastFrameMs = millis();
      return;
    }

    pet.holding              = true;
    pet.holdDuration         = 0.0f;
    pet.dizzyTriggeredInHold = false;
    pet.idleCountdown        = IDLE_INTERVAL_SEC;
  } else if (rawTouch && pet.holding) {
    pet.noTouchElapsed = 0.0f;
    pet.holdDuration += dt;
    if (!pet.dizzyTriggeredInHold && pet.holdDuration >= HOLD_DIZZY_SEC) {
      pet.dizzyTriggeredInHold = true;
      setPetMode(MODE_DIZZY, 999.0f, true, true);
    }
  } else if (!rawTouch && pet.holding) {
    float held       = pet.holdDuration;
    pet.holding      = false;
    pet.holdDuration = 0.0f;

    if (!pet.dizzyTriggeredInHold && held >= 0.025f && held < HOLD_DIZZY_SEC) {
      PetMode next = TAP_SEQUENCE[seqIndex % TAP_SEQ_LEN];
      seqIndex++;
      setPetMode(next, (next == MODE_IDLE) ? 999.0f : 2.6f, true, true);
    } else if (pet.mode == MODE_DIZZY) {
      pet.modeDuration = pet.animElapsed + 2.4f;
    }
  }
}

// ======================= KHỞI TẠO & VÒNG LẶP CHÍNH =======================
void setup() {
  pinMode(PIN_TOUCH, INPUT);
  if (PIN_BUZZER >= 0) pinMode(PIN_BUZZER, OUTPUT);
  if (PIN_TFT_BL >= 0) {
    pinMode(PIN_TFT_BL, OUTPUT);
    digitalWrite(PIN_TFT_BL, HIGH);
  }

  tft.init();
  tft.setRotation(SCREEN_ROTATION);
  tft.fillScreen(TFT_BLACK);

  initPalette();

  canvas.setColorDepth(16);
  if (!canvas.createSprite(240, 240)) {
    canvas.setColorDepth(8);
    canvas.createSprite(240, 240);
  }

  lastFrameMs = millis();
  setPetMode(MODE_BOOT, 3.1f, true, true);
}

void loop() {
  unsigned long nowMs = millis();
  float dt = (nowMs - lastFrameMs) / 1000.0f;
  if (dt > 0.05f) dt = 0.05f;
  lastFrameMs = nowMs;
  float t = nowMs / 1000.0f;

  pet.animElapsed += dt;

  // 1. Cảm biến chạm
  updateTouch(dt);

  // 2. Quản lý chu kỳ 15s tự diễn & 60s tự ngủ
  if (!pet.holding) {
    if (pet.mode == MODE_FALL_ASLEEP) {
      if (pet.animElapsed >= pet.modeDuration) {
        pet.mode        = MODE_SLEEP;
        pet.animElapsed = 0.0f;
        canvas.fillSprite(colBg);
        canvas.pushSprite(0, 0);
        setBacklight(false);
      }
    } else if (pet.mode == MODE_SLEEP) {
      // Ngủ sâu tiết kiệm điện, chờ người dùng chạm
      delay(30);
      return;
    } else if (pet.mode != MODE_IDLE) {
      pet.noTouchElapsed += dt;
      if (pet.animElapsed >= pet.modeDuration) {
        pet.mode          = MODE_IDLE;
        pet.animElapsed   = 0.0f;
        pet.idleCountdown = IDLE_INTERVAL_SEC;
      }
    } else {
      pet.noTouchElapsed += dt;
      pet.idleCountdown  -= dt;

      if (pet.noTouchElapsed >= AUTO_SLEEP_SEC) {
        setPetMode(MODE_FALL_ASLEEP, 2.5f, true, false);
      } else if (pet.idleCountdown <= 0.0f) {
        PetMode nextStory = IDLE_STORIES[storyIndex % IDLE_STORY_LEN];
        storyIndex++;
        float dur = (nextStory == MODE_FORTUNE) ? 4.3f : 3.9f;
        setPetMode(nextStory, dur, true, false);
      }
    }
  }

  // 3. Chớp mắt tự nhiên
  pet.nextBlink -= dt;
  if (pet.nextBlink <= 0.0f && !pet.blinking) {
    pet.blinking   = true;
    pet.blinkPhase = 0.0f;
    pet.nextBlink  = 2.4f + (random(0, 280) / 100.0f);
  }
  if (pet.blinking) {
    pet.blinkPhase += dt * 11.0f;
    if (pet.blinkPhase >= 3.14159f) {
      pet.blinking = false;
      pet.blink    = 1.0f;
    } else {
      float b = 1.0f - sinf(pet.blinkPhase);
      pet.blink = (b < 0.08f) ? 0.08f : b;
    }
  }

  // 4. Đảo mắt nhẹ khi rảnh
  pet.nextGaze -= dt;
  if (pet.nextGaze <= 0.0f) {
    pet.nextGaze = 1.6f + (random(0, 240) / 100.0f);
    if (random(0, 100) < 55) {
      pet.targetGazeX = 0.0f;
      pet.targetGazeY = 0.0f;
    } else {
      pet.targetGazeX = (random(-55, 56) / 10.0f);
      pet.targetGazeY = (random(-30, 31) / 10.0f);
    }
  }

  // 5. Nội suy mượt
  float lerpK = dt * 12.0f;
  if (lerpK > 1.0f) lerpK = 1.0f;

  pet.gazeX    += (pet.targetGazeX - pet.gazeX) * lerpK;
  pet.gazeY    += (pet.targetGazeY - pet.gazeY) * lerpK;
  pet.bounce    = (pet.bounce - dt * 5.5f > 0.0f) ? (pet.bounce - dt * 5.5f) : 0.0f;

  pet.angerMix += (((pet.mode == MODE_ANGRY) ? 1.0f : 0.0f) - pet.angerMix) * lerpK;
  pet.happyMix += (((pet.mode == MODE_HAPPY) ? 1.0f : 0.0f) - pet.happyMix) * lerpK;
  pet.winkMix  += (((pet.mode == MODE_WINK)  ? 1.0f : 0.0f) - pet.winkMix)  * lerpK;

  // 6. Vẽ khung hình lên bộ đệm 240x240 và xuất ra màn hình
  canvas.fillSprite(colBg);

  switch (pet.mode) {
    case MODE_FALL_ASLEEP:
      drawFallAsleepScene(pet.animElapsed);
      break;
    case MODE_WAKEUP:
      drawWakeUpScene(pet.animElapsed);
      break;
    case MODE_BOOT:
      drawBootHiScene(pet.animElapsed);
      break;
    case MODE_DIZZY:
      drawDizzyScene(t);
      break;
    case MODE_BASEBALL:
      drawBaseballScene(pet.animElapsed, t);
      break;
    case MODE_FORTUNE:
      drawFortuneScene(pet.animElapsed, t);
      break;
    case MODE_HUMMING:
      drawHummingScene(pet.animElapsed, t);
      break;
    default:
      drawMainPetFace(t);
      break;
  }

  canvas.pushSprite(0, 0);
}
