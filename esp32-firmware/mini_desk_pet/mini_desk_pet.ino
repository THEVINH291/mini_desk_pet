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
LGFX_Sprite  charSpr(&canvas);

// ======================= TRẠNG THÁI HOẠT ĐỘNG =======================
enum PetMode {
  MODE_BOOT = 0,     // Màn chào "Hi" khi mới cấp nguồn
  MODE_IDLE,         // Bình thường
  MODE_WINK,         // Nháy mắt (o <)
  MODE_ANGRY,        // Cau mày (\ /)
  MODE_HAPPY,        // Cười tươi (:D)
  MODE_MELT,         // Nóng đỏ mặt -> Tan chảy -> Sôi bốc hơi (🫠)
  MODE_FREEZE,       // Lạnh run cầm cập -> Đóng tảng băng -> Phá băng vỡ tung (🥶)
  MODE_BUBBLE,       // Phồng tròn hóa thành bong bóng ngũ sắc rồi bay bổng lên trời (🫧)
  MODE_MINDBLOWN,    // Quá tải phồng đầu -> Nổ tung đám mây hình nấm (🤯)
  MODE_BIRTHDAY,     // Đội mũ sinh nhật + Bánh kem thổi nến + Nhạc Happy Birthday (🎂)
  MODE_DIZZY,        // Choáng váng khi giữ lâu (@ @ + sao quay quanh đầu)
  MODE_BASEBALL,     // Hoạt ảnh 15s #1: Đội mũ vàng đánh bóng chày Home Run
  MODE_FORTUNE,      // Hoạt ảnh 15s #2: Nhảy trên dòng chữ "Luck is Yours"
  MODE_HUMMING,      // Hoạt ảnh 15s #3: Ngân nga nốt nhạc ♫ + mây bọt
  MODE_FALL_ASLEEP,  // Chuyển sang lim dim rồi tắt màn
  MODE_SLEEP,        // Ngủ sâu (màn hình tối đen hoàn toàn)
  MODE_WAKEUP        // Thức dậy 3 giai đoạn khi chạm lúc đang ngủ
};

const PetMode TAP_SEQUENCE[] = {
  MODE_WINK, MODE_ANGRY, MODE_HAPPY, MODE_MINDBLOWN, MODE_BUBBLE,
  MODE_IDLE, MODE_FREEZE, MODE_MELT, MODE_WINK, MODE_HAPPY
};
const int TAP_SEQ_LEN = sizeof(TAP_SEQUENCE) / sizeof(TAP_SEQUENCE[0]);

const PetMode IDLE_STORIES[] = {
  MODE_BASEBALL, MODE_FORTUNE, MODE_FREEZE, MODE_MELT,
  MODE_BUBBLE, MODE_MINDBLOWN, MODE_BIRTHDAY, MODE_HUMMING
};
const int IDLE_STORY_LEN = sizeof(IDLE_STORIES) / sizeof(IDLE_STORIES[0]);

const char* const LUCKY_WISHES[] = {
  "Luck is Yours",
  "Stay Happy :)",
  "Good Vibes Only",
  "Big Win Ahead!",
  "You Got This!",
  "Shine Bright!",
  "Money & Joy!",
  "Best Day Ever",
  "Dream Big!",
  "Keep Smiling!"
};
const int LUCKY_WISH_LEN = sizeof(LUCKY_WISHES) / sizeof(LUCKY_WISHES[0]);

int seqIndex   = 0;
int storyIndex = 0;
int wishIndex  = 0;

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
  bool    boilPlayed           = false;
  bool    iceLockPlayed        = false;
  bool    shatterPlayed        = false;
  bool    boomPlayed           = false;
  int     bdayNoteIdx          = 0;
  const char* currentWish      = "Luck is Yours";
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
uint16_t colRibbonText;
uint16_t colTransp;

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
  colRibbonText  = tft.color565(15, 23, 42);
  colTransp      = tft.color565(255, 0, 255);
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
    case MODE_MELT:
      tone(PIN_BUZZER, 500, 45); delay(45);
      tone(PIN_BUZZER, 920, 55); delay(55);
      tone(PIN_BUZZER, 640, 55); delay(55);
      tone(PIN_BUZZER, 320, 80);
      break;
    case MODE_FREEZE:
      tone(PIN_BUZZER, 920, 35);  delay(45);
      tone(PIN_BUZZER, 1180, 35); delay(45);
      tone(PIN_BUZZER, 920, 35);  delay(45);
      tone(PIN_BUZZER, 1180, 35);
      break;
    case MODE_BUBBLE:
      tone(PIN_BUZZER, 523, 45);  delay(45);
      tone(PIN_BUZZER, 784, 45);  delay(45);
      tone(PIN_BUZZER, 1046, 55); delay(55);
      tone(PIN_BUZZER, 1568, 80);
      break;
    case MODE_MINDBLOWN:
      tone(PIN_BUZZER, 320, 45);  delay(45);
      tone(PIN_BUZZER, 580, 45);  delay(45);
      tone(PIN_BUZZER, 980, 45);  delay(45);
      tone(PIN_BUZZER, 1520, 65);
      break;
    case MODE_BIRTHDAY:
      // Giai điệu Happy Birthday được phát không chặn (non-blocking) ngay trong drawBirthdayScene!
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
  if (newMode == MODE_FORTUNE) {
    pet.currentWish = LUCKY_WISHES[wishIndex % LUCKY_WISH_LEN];
    wishIndex++;
  }
  pet.bounce        = 1.0f;
  pet.batHitPlayed  = false;
  pet.popPlayed     = false;
  pet.boilPlayed    = false;
  pet.iceLockPlayed = false;
  pet.shatterPlayed = false;
  pet.boomPlayed    = false;
  pet.bdayNoteIdx   = 0;
  if (sound) playChirp(newMode);
}

// ======================= CÁC HÀM VẼ CƠ BẢN =======================
void drawMochiHead(float cx, float cy, float rx, float ryTop, float ryBot,
                   float scale = 1.0f, float tiltRad = 0.0f, float maxLocalY = 999.0f, float minLocalY = -999.0f) {
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

    vis[i] = (ly >= minLocalY && ly <= maxLocalY);
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

// ======================= 4. BÁNH MAY MẮN: DẢI LỤA 3D UỐN LƯỢN + CHỮ CONG THEO SÓNG =======================
float fortuneWaveOffset(float u, float extraDip = 0.0f) {
  return -8.5f * cosf(u * 3.14159f * 1.42f) + (3.2f + extraDip) * sinf(u * 3.14159f);
}

void drawTriangleCrumb(float cx, float cy, float sz, float rot, uint16_t color) {
  float x0 = cx + sinf(rot) * sz;
  float y0 = cy - cosf(rot) * sz;
  float x1 = cx + sinf(rot + 2.1f) * sz;
  float y1 = cy - cosf(rot + 2.1f) * sz;
  float x2 = cx + sinf(rot + 4.2f) * sz;
  float y2 = cy - cosf(rot + 4.2f) * sz;
  canvas.fillTriangle((int)x0, (int)y0, (int)x1, (int)y1, (int)x2, (int)y2, color);
}

void draw3DWavyScroll(int centerY, float openP, const char* wishText, float extraDip = 0.0f) {
  float halfW  = 70.0f * openP;
  int   xLeft  = 118 - (int)halfW;
  int   xRight = 118 + (int)halfW;
  int   spanW  = xRight - xLeft;
  const int ribbonH = 33;

  // 1) Đầu cuộn vàng khối 3D bên trái
  int ly = centerY + (int)fortuneWaveOffset(0.0f, extraDip);
  canvas.fillTriangle(xLeft - 4, ly - 22, xLeft + 10, ly - 15, xLeft + 8, ly + 18, colGoldDark);
  canvas.fillTriangle(xLeft - 4, ly - 22, xLeft + 8,  ly + 18, xLeft - 4, ly + 22, colGoldDark);
  canvas.fillTriangle(xLeft - 17, ly - 21, xLeft - 2, ly - 22, xLeft - 1,  ly + 19, colYellowLight);
  canvas.fillTriangle(xLeft - 17, ly - 21, xLeft - 1, ly + 19, xLeft - 17, ly + 24, colYellow);

  // 2) Đầu cuộn vàng khối 3D bên phải (2 mặt vát khối 3D giống hệt ảnh mẫu)
  int ry = centerY + (int)fortuneWaveOffset(1.0f, extraDip);
  canvas.fillTriangle(xRight - 8, ry - 20, xRight + 6, ry - 25, xRight + 6, ry + 20, colGoldDark);
  canvas.fillTriangle(xRight - 3, ry - 24, xRight + 6, ry - 20, xRight + 6, ry + 25, colYellow);
  canvas.fillTriangle(xRight - 3, ry - 24, xRight + 6, ry + 25, xRight - 3, ry + 19, colGoldDark);
  canvas.fillTriangle(xRight + 5, ry - 23, xRight + 23, ry - 16, xRight + 22, ry + 23, colYellowLight);
  canvas.fillTriangle(xRight + 5, ry - 23, xRight + 22, ry + 23, xRight + 5,  ry + 26, colYellow);

  if (spanW < 24) return;

  // 3) Thân dải lụa uốn lượn chữ S kèm đổ bóng 3D trắng -> xanh băng ở hõm giữa -> trắng
  for (int ix = xLeft; ix <= xRight; ix += 2) {
    float u = (float)(ix - xLeft) / (float)spanW;
    int wy  = centerY + (int)fortuneWaveOffset(u, extraDip);
    int topY = wy - ribbonH / 2;

    // Tính độ trũng để pha màu bóng 3D xanh băng ở phần hõm lượn
    float valley = sinf(u * 3.14159f);
    valley = valley * valley;
    uint8_t rCol = (uint8_t)(250.0f - valley * 102.0f);
    uint8_t gCol = (uint8_t)(252.0f - valley * 54.0f);
    uint8_t bCol = 254;
    uint16_t stripCol = tft.color565(rCol, gCol, bCol);

    canvas.fillRect(ix, topY, 2, ribbonH, stripCol);
    canvas.fillRect(ix, topY - 1, 2, 2, colSkyBlue);
    canvas.fillRect(ix, topY + ribbonH - 1, 2, 2, colSkyBlue);
  }

  // 4) Vẽ từng ký tự nghiêng & uốn lượn dọc theo đúng đường cong của dải lụa!
  if (openP > 0.68f && wishText != nullptr) {
    charSpr.setFont(&fonts::FreeSansBold9pt7b);
    int len = strlen(wishText);
    int totalW = 0;
    int charW[24];
    for (int i = 0; i < len && i < 24; i++) {
      char buf[2] = { wishText[i], '\0' };
      int w = charSpr.textWidth(buf);
      if (wishText[i] == ' ') w = 5;
      charW[i] = w + 1;
      totalW += charW[i];
    }

    float curX = (xLeft + xRight) * 0.5f - totalW * 0.5f;
    for (int i = 0; i < len && i < 24; i++) {
      int cw = charW[i];
      float charCx = curX + cw * 0.5f;
      if (wishText[i] != ' ') {
        float u = (charCx - xLeft) / (float)spanW;
        if (u < 0.03f) u = 0.03f;
        if (u > 0.97f) u = 0.97f;
        float charCy = (float)centerY + fortuneWaveOffset(u, extraDip) + 1.0f;

        float du = 0.03f;
        float u1 = (u + du < 1.0f) ? (u + du) : 1.0f;
        float u0 = (u - du > 0.0f) ? (u - du) : 0.0f;
        float dy = fortuneWaveOffset(u1, extraDip) - fortuneWaveOffset(u0, extraDip);
        float dx = (u1 - u0) * (float)spanW;
        float deg = atan2f(dy, dx) * 57.29578f;

        char buf[2] = { wishText[i], '\0' };
        charSpr.fillSprite(colTransp);
        charSpr.setTextColor(colRibbonText);
        charSpr.setCursor(12 - cw / 2, 4);
        charSpr.print(buf);
        charSpr.pushRotateZoom(charCx, charCy, deg, 1.0f, 1.0f, colTransp);
      }
      curX += cw;
    }
  }
}

void drawFortuneScene(float el, float t) {
  if (el < 1.05f) {
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
  } else if (el < 2.55f) {
    // Pha 2 (Giống hệt ảnh mẫu!): Bé Pet ló đầu nháy mắt (• <) sau mép phải dải lụa 3D + vụn tam giác vàng!
    float openP = (el - 1.05f) / 0.35f;
    if (openP > 1.0f) openP = 1.0f;
    int scrollY = 128;

    if (openP > 0.55f) {
      float peekPop = (openP - 0.55f) / 0.45f;
      if (peekPop > 1.0f) peekPop = 1.0f;
      float px = 148.0f;
      float py = (float)scrollY - 12.0f + (1.0f - peekPop) * 14.0f;
      drawMochiHead(px, py, 64.0f, 56.0f, 46.0f, 0.31f);
      drawPillEye((int)px - 7, (int)py - 2, 4, 7, colWhite);
      canvas.drawWideLine(px + 9, py - 5, px + 4, py - 1.5f, 2.4f, colWhite);
      canvas.drawWideLine(px + 4, py - 1.5f, px + 9, py + 2, 2.4f, colWhite);
    }

    draw3DWavyScroll(scrollY, openP, pet.currentWish, 0.0f);

    if (openP > 0.40f) {
      float drift = sinf(t * 5.0f) * 1.5f;
      drawTriangleCrumb(70.0f,  scrollY - 38.0f + drift, 6.5f,  0.25f, colYellowLight);
      drawTriangleCrumb(56.0f,  scrollY + 26.0f - drift, 4.5f,  0.95f, colYellow);
      drawTriangleCrumb(156.0f, scrollY + 31.0f + drift, 5.5f, -0.35f, colYellowLight);
      drawTriangleCrumb(167.0f, scrollY + 32.0f - drift, 5.0f,  0.55f, colGoldDark);
    }
  } else {
    // Pha 3: Pet nhảy ra và nhún tưng tưng ngay trên dải lụa 3D uốn lượn!
    float settleP = (el - 2.55f) / 0.28f;
    if (settleP > 1.0f) settleP = 1.0f;
    float jumpPhase    = (el - 2.55f) * 8.2f;
    float hopSin       = fabsf(sinf(jumpPhase));
    float hopHeight    = hopSin * 26.0f;
    float landingPress = (hopSin < 0.22f) ? (1.0f - hopSin / 0.22f) : 0.0f;

    int scrollY = 128 + (int)(settleP * 24.0f + landingPress * 2.5f);
    draw3DWavyScroll(scrollY, 1.0f, pet.currentWish, landingPress * 3.5f);

    drawTriangleCrumb(66.0f,  88.0f - hopSin * 4.0f, 6.0f,  t * 2.0f, colYellowLight);
    drawTriangleCrumb(176.0f, 92.0f - hopSin * 4.0f, 5.5f, -t * 2.0f, colSkyBlue);
    drawTriangleCrumb(160.0f, scrollY + 30.0f,       4.8f,  0.4f,     colYellow);

    float ribbonTopAtCenter = (float)scrollY + fortuneWaveOffset(0.52f, landingPress * 3.5f) - 16.5f;
    float petScale   = 0.44f;
    float sx         = 1.0f + landingPress * 0.14f - hopSin * 0.05f;
    float sy         = 1.0f - landingPress * 0.14f + hopSin * 0.06f;
    float petBottomR = 46.0f * petScale * sy + 3.0f;
    float petCx      = 148.0f * (1.0f - settleP) + 120.0f * settleP;
    float petCy      = ribbonTopAtCenter - petBottomR - hopHeight;

    drawMochiHead(petCx, petCy, 64.0f * sx, 56.0f * sy, 46.0f * sy, petScale);

    if (hopSin > 0.55f) {
      drawUpwardArcEye(petCx, petCy, -10.0f, 3.0f, 5.2f, 3.0f, 0.0f, colWhite);
      drawUpwardArcEye(petCx, petCy,  10.0f, 3.0f, 5.2f, 3.0f, 0.0f, colWhite);
    } else {
      drawPillEye((int)petCx - 10, (int)petCy - 1, 5, (int)(9.5f * sy), colWhite);
      drawPillEye((int)petCx + 10, (int)petCy - 1, 5, (int)(9.5f * sy), colWhite);
    }
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

// ======================= 6.5 NÓNG ĐỎ MẶT -> TAN CHẢY -> SÔI SÙNG SỤC & BỐC HƠI =======================
void drawMeltScene(float el, float t) {
  // Pha 4 (4.35s -> 5.00s): Bốc hơi hết -> Pet xanh mát lạnh bật nảy xuất hiện trở lại!
  if (el >= 4.35f) {
    float rp = (el - 4.35f) / 0.55f;
    if (rp > 1.0f) rp = 1.0f;
    float p1 = rp - 1.0f;
    float easeBack = 1.0f + 2.2f * p1 * p1 * p1 + 1.2f * p1 * p1;
    float sc = 0.25f + 0.75f * easeBack;
    if (sc < 0.1f) sc = 0.1f;

    drawMochiHead(120.0f, 122.0f, 64.0f, 56.0f, 46.0f, sc);
    drawPillEye(120 - (int)(24.0f * sc), 120, (int)(13.0f * sc), (int)(24.0f * sc), colWhite);
    drawPillEye(120 + (int)(24.0f * sc), 120, (int)(13.0f * sc), (int)(24.0f * sc), colWhite);
    return;
  }

  // Pha 1: Nóng lên đỏ mặt (0.0 -> 1.05s)
  float heat = el / 1.05f;
  if (heat > 1.0f) heat = 1.0f;

  // Pha 2: Tan chảy sụp thành vũng (1.20 -> 2.50s)
  float m = 0.0f;
  if (el > 1.20f) {
    float mp = (el - 1.20f) / 1.30f;
    if (mp > 1.0f) mp = 1.0f;
    m = mp * mp * (3.0f - 2.0f * mp);
  }

  // Pha 3: Sôi sùng sục như nước sôi & bốc hơi cạn dần (2.60 -> 4.35s)
  float boilP = 0.0f;
  if (el > 2.60f) {
    if (!pet.boilPlayed) {
      pet.boilPlayed = true;
      if (ENABLE_SOUND && PIN_BUZZER >= 0) {
        tone(PIN_BUZZER, 540, 30); delay(30);
        tone(PIN_BUZZER, 820, 30); delay(30);
        tone(PIN_BUZZER, 480, 30); delay(30);
        tone(PIN_BUZZER, 960, 40);
      }
    }
    boilP = (el - 2.60f) / 1.70f;
    if (boilP > 1.0f) boilP = 1.0f;
  }

  float evapRemain = 1.0f - powf(boilP, 1.35f);
  float floorY     = 186.0f;

  // Pha màu trực tiếp từ Xanh Mochi -> Đỏ rực nóng bừng
  uint8_t rMain = (uint8_t)(36.0f  + heat * 219.0f);
  uint8_t gMain = (uint8_t)(155.0f - heat * 90.0f + boilP * 35.0f);
  uint8_t bMain = (uint8_t)(255.0f - heat * 200.0f);
  uint16_t colHotMain = tft.color565(rMain, gMain, bMain);

  uint8_t rHalo = (uint8_t)(14.0f + heat * 125.0f);
  uint8_t gHalo = (uint8_t)(58.0f - heat * 42.0f);
  uint8_t bHalo = (uint8_t)(138.0f - heat * 115.0f);
  uint16_t colHotHalo = tft.color565(rHalo, gHalo, bHalo);
  uint16_t colHotCore = (heat > 0.5f) ? tft.color565(254, 202, 202) : colIceBlue;
  uint16_t colBlush   = tft.color565(244, 63, 94);

  float tremble = (el < 1.35f) ? sinf(t * 48.0f) * 2.5f * heat
                               : sinf(t * 36.0f) * 2.2f * ((boilP > 0.0f) ? 1.0f : 0.3f);
  float cx = 120.0f + tremble;
  float cy = 122.0f + m * 46.0f + (1.0f - evapRemain) * 16.0f;

  if (evapRemain > 0.05f) {
    float hScale = 0.25f + 0.75f * evapRemain;
    float vScale = evapRemain;
    const int STEPS = 44;
    float px[STEPS + 1];
    float py[STEPS + 1];

    for (int i = 0; i <= STEPS; i++) {
      float a  = (i * 2.0f * 3.1415926f) / STEPS;
      float sa = sinf(a);
      float ca = cosf(a);

      float posSa      = (sa > 0.0f) ? sa : 0.0f;
      float rxBase     = 64.0f * (1.0f + m * 0.42f) * hScale * (1.0f + (0.045f + m * 0.22f) * posSa);
      float boilRipple = (boilP > 0.0f) ? sinf(a * 7.0f + t * 26.0f) * 3.5f * vScale : 0.0f;
      float puddleWave = m * ((sa > -0.1f) ? cosf(a * 4.0f + t * 6.0f) * 3.5f : sinf(a * 3.0f - t * 5.0f) * 1.8f) * vScale + boilRipple;

      float ryTop = 56.0f * (1.0f - m * 0.56f) * vScale;
      float ryBot = 46.0f * (1.0f - m * 0.52f) * vScale;
      float ry    = (ryTop + ryBot) * 0.5f - (ryTop - ryBot) * 0.5f * sa;

      px[i] = cx + ca * (rxBase + puddleWave);
      float yVal = cy + sa * ry;
      if (yVal > floorY) {
        yVal = floorY + (yVal - floorY) * (1.0f - m * 0.86f);
      }
      py[i] = yVal;
    }

    float thickScale = (vScale > 0.45f) ? vScale : 0.45f;
    for (int i = 0; i < STEPS; i++) canvas.drawWideLine(px[i], py[i], px[i + 1], py[i + 1], 13.0f * thickScale, colHotHalo);
    for (int i = 0; i < STEPS; i++) canvas.drawWideLine(px[i], py[i], px[i + 1], py[i + 1], 9.0f  * thickScale, colHotMain);
    for (int i = 0; i < STEPS; i++) canvas.drawWideLine(px[i], py[i], px[i + 1], py[i + 1], 2.4f  * thickScale, colHotCore);

    // Hai má ửng đỏ hồng + gạch chéo /// xấu hổ / nóng bừng
    if (heat > 0.25f && evapRemain > 0.35f) {
      float cheekX = (36.0f + m * 10.0f) * hScale;
      int   cheekY = (int)(cy + 8.0f - m * 2.0f);
      int   crx    = (int)(12.0f * hScale);
      int   cry    = (int)(6.5f * vScale);
      if (cry < 2) cry = 2;
      canvas.fillEllipse((int)(cx - cheekX), cheekY, crx, cry, colBlush);
      canvas.fillEllipse((int)(cx + cheekX), cheekY, crx, cry, colBlush);

      for (int k = -1; k <= 1; k++) {
        int lx = (int)(cx - cheekX) + k * 4;
        int rx = (int)(cx + cheekX) + k * 4;
        canvas.drawLine(lx + 2, cheekY - 2, lx - 2, cheekY + 2, colHotCore);
        canvas.drawLine(rx + 2, cheekY - 2, rx - 2, cheekY + 2, colHotCore);
      }
    }

    // Giọt mồ hôi lăn xuống khi mới nóng đỏ mặt
    if (heat > 0.35f && boilP < 0.3f) {
      float sweatY = fmodf(el * 2.2f, 1.0f) * 24.0f;
      canvas.fillSmoothCircle((int)(cx + (48.0f + m * 14.0f) * hScale), (int)(cy - 22.0f * vScale + sweatY), 3, colIceBlue);
    }

    // Mắt lệch & miệng gợn sóng ﹏ (thu nhỏ dần khi bốc hơi)
    if (evapRemain > 0.28f) {
      float eyeLen = (19.0f - m * 9.0f) * vScale * pet.blink;
      if (eyeLen < 3.0f) eyeLen = 3.0f;
      float halfL = eyeLen * 0.5f;
      float eyeW  = (12.5f + m * 2.0f) * ((hScale > 0.6f) ? hScale : 0.6f);

      float lx = cx - (23.0f + m * 5.0f) * hScale;
      float ly = cy - 2.0f * vScale;
      float la = -0.24f * m;
      canvas.drawWideLine(lx + sinf(la) * halfL, ly - cosf(la) * halfL,
                          lx - sinf(la) * halfL, ly + cosf(la) * halfL, eyeW, colWhite);

      float rx = cx + (23.0f + m * 6.0f) * hScale;
      float ry = cy + (-2.0f + m * 5.0f) * vScale;
      float ra = 0.28f * m;
      canvas.drawWideLine(rx + sinf(ra) * halfL, ry - cosf(ra) * halfL,
                          rx - sinf(ra) * halfL, ry + cosf(ra) * halfL, eyeW, colWhite);

      if (heat > 0.4f) {
        float my = cy + (15.0f + m * 3.0f) * vScale;
        float mw = 16.0f * hScale;
        canvas.drawWideLine(cx - mw,        my - 1.0f, cx - mw * 0.4f, my + 4.0f * vScale, 3.4f * thickScale, colWhite);
        canvas.drawWideLine(cx - mw * 0.4f, my + 4.0f * vScale, cx + mw * 0.4f, my - 2.0f * vScale, 3.4f * thickScale, colWhite);
        canvas.drawWideLine(cx + mw * 0.4f, my - 2.0f * vScale, cx + mw,        my + 2.0f * vScale, 3.4f * thickScale, colWhite);
      }
    }
  }

  // Bong bóng nước sôi sùng sục o O nổ lách tách trên mặt vũng!
  if (el > 2.35f && evapRemain > 0.08f) {
    float puddleTopY = cy - 22.0f * evapRemain;
    float puddleSpan = 72.0f * (0.3f + 0.7f * evapRemain);
    for (int b = 0; b < 7; b++) {
      float raw = el * 3.6f + b * 0.29f;
      float bp  = fmodf(raw, 1.0f);
      int   bx  = 120 + (int)(sinf(b * 2.1f + floorf(raw)) * puddleSpan);
      int   by  = (int)(puddleTopY + cosf(b * 1.7f) * 5.0f * evapRemain - bp * 10.0f);
      if (bp < 0.80f) {
        int br = (int)((2.0f + (bp / 0.80f) * 6.0f) * ((evapRemain > 0.45f) ? evapRemain : 0.45f));
        canvas.drawCircle(bx, by, br, colHotCore);
      } else {
        canvas.drawLine(bx - 6, by - 6, bx - 3, by - 3, colHotCore);
        canvas.drawLine(bx + 6, by - 6, bx + 3, by - 3, colHotCore);
        canvas.drawLine(bx - 6, by + 6, bx - 3, by + 3, colHotCore);
        canvas.drawLine(bx + 6, by + 6, bx + 3, by + 3, colHotCore);
      }
    }
  }

  // Khói trắng & hơi nước bốc lên nghi ngút!
  if (el > 1.0f) {
    int count = (el > 2.5f) ? 7 : 3;
    for (int s = 0; s < count; s++) {
      float sp = fmodf(el * 0.78f + s * 0.19f, 1.0f);
      float startY = (el > 2.5f) ? 172.0f : 95.0f;
      int   sy = (int)(startY - sp * 126.0f);
      int   sx = 120 + (int)(sinf(s * 2.4f) * 44.0f + sinf(sp * 7.0f + s) * 10.0f);

      float fade = sinf(sp * 3.14159f);
      if (fade > 0.15f) {
        uint8_t shade = (uint8_t)(210.0f * fade);
        uint16_t steamCol = (el > 2.4f) ? tft.color565(shade, shade, shade + 15) : tft.color565(shade, shade / 3, shade / 3);

        if (el > 2.4f) {
          int pr = 6 + (int)(sp * 11.0f);
          canvas.fillSmoothCircle(sx,          sy,     pr,          steamCol);
          canvas.fillSmoothCircle(sx - pr / 2, sy + 3, (pr * 7) / 10, steamCol);
          canvas.fillSmoothCircle(sx + pr / 2, sy + 3, (pr * 7) / 10, steamCol);
        } else {
          canvas.drawWideLine(sx, sy + 8, sx - 4, sy + 2, 2.2f, steamCol);
          canvas.drawWideLine(sx - 4, sy + 2, sx + 4, sy - 5, 2.2f, steamCol);
          canvas.drawWideLine(sx + 4, sy - 5, sx, sy - 11, 2.2f, steamCol);
        }
      }
    }
  }
}

// ======================= 6.6 LẠNH CÓNG -> ĐÓNG TẢNG BĂNG -> PHÁ BĂNG VỠ TUNG (🥶) =======================
void drawFreezeScene(float el, float t) {
  uint16_t colFrostBg   = tft.color565(10, 56, 82);
  uint16_t colFrostCyan = tft.color565(103, 232, 249);
  uint16_t colFrostRim  = tft.color565(207, 250, 254);

  if (el < 3.55f) {
    // Tuyết rơi lất phất từ trên xuống
    for (int i = 0; i < 6; i++) {
      float sp = fmodf(el * 0.55f + i * 0.17f, 1.0f);
      int   sx = 30 + ((i * 31) % 180) + (int)(sinf(sp * 6.0f + i) * 8.0f);
      int   sy = 16 + (int)(sp * 195.0f);
      canvas.drawLine(sx - 4, sy, sx + 4, sy, colFrostRim);
      canvas.drawLine(sx, sy - 4, sx, sy + 4, colFrostRim);
    }

    bool isFrozenSolid = (el >= 1.25f);
    if (isFrozenSolid && !pet.iceLockPlayed) {
      pet.iceLockPlayed = true;
      if (ENABLE_SOUND && PIN_BUZZER >= 0) {
        tone(PIN_BUZZER, 1318, 45); delay(45);
        tone(PIN_BUZZER, 2093, 75);
      }
    }

    float chill       = (el < 1.0f) ? el : 1.0f;
    float crackPhase  = (el > 2.65f) ? ((el - 2.65f) / 0.90f) : 0.0f;
    float blockRattle = (crackPhase > 0.0f) ? sinf(t * 58.0f) * (crackPhase * 4.5f) : 0.0f;
    float petShiver   = isFrozenSolid ? 0.0f : sinf(t * 62.0f) * (1.5f + chill * 3.5f);

    int   iceW    = 168;
    float iceGrow = (el > 1.15f) ? ((el - 1.15f) / 0.22f) : 0.0f;
    if (iceGrow > 1.0f) iceGrow = 1.0f;
    int   iceH = (int)(148.0f * iceGrow);
    int   ix   = 120 - iceW / 2 + (int)blockRattle;
    int   iy   = 198 - iceH;

    // 1) Lớp nền băng xanh thẫm vẽ PHÍA SAU Pet để tạo hiệu ứng tảng băng trong suốt!
    if (iceGrow > 0.05f) {
      canvas.fillSmoothRoundRect(ix, iy, iceW, iceH, 14, colFrostBg);
    }

    // 2) Vẽ Pet đang run cầm cập rồi đông cứng bên trong tảng băng
    float cx = 120.0f + petShiver + blockRattle;
    float cy = 126.0f;
    drawMochiHead(cx, cy, 64.0f, 56.0f, 46.0f, 1.0f);

    // Mắt nheo chặt > < vì lạnh cóng
    canvas.drawWideLine(cx - 31, cy - 11, cx - 17, cy - 3, 4.2f, colWhite);
    canvas.drawWideLine(cx - 17, cy - 3,  cx - 31, cy + 5, 4.2f, colWhite);
    canvas.drawWideLine(cx + 31, cy - 11, cx + 17, cy - 3, 4.2f, colWhite);
    canvas.drawWideLine(cx + 17, cy - 3,  cx + 31, cy + 5, 4.2f, colWhite);

    // Giọt nước mũi đóng thành nhũ băng nhỏ!
    if (chill > 0.35f) {
      int icLen = (int)(((chill - 0.35f) / 0.65f) * 13.0f);
      canvas.fillTriangle((int)cx + 3, (int)cy + 3, (int)cx + 9, (int)cy + 3, (int)cx + 6, (int)cy + 3 + icLen, colFrostRim);
    }

    // Hàm răng lập cập ziczac MWMW!
    float jaw = isFrozenSolid ? 0.0f : sinf(t * 55.0f) * 1.5f;
    float my  = cy + 21.0f + jaw;
    for (int k = -2; k < 2; k++) {
      float x0 = cx + k * 8.0f;
      float y0 = my + ((k % 2 == 0) ? -3.5f : 3.5f);
      float x1 = cx + (k + 1) * 8.0f;
      float y1 = my + (((k + 1) % 2 == 0) ? -3.5f : 3.5f);
      canvas.drawWideLine(x0, y0, x1, y1, 2.6f, colFrostRim);
    }

    // 3) Vẽ viền tảng băng + vệt lóa kính chéo + nhũ băng + vết nứt ĐÈ LÊN TRƯỚC Pet!
    if (iceGrow > 0.05f) {
      canvas.drawRoundRect(ix,     iy,     iceW,     iceH,     14, colFrostRim);
      canvas.drawRoundRect(ix + 1, iy + 1, iceW - 2, iceH - 2, 13, colFrostCyan);

      if (iceGrow > 0.85f) {
        // Vệt phản quang mặt băng chéo tạo độ trong suốt
        canvas.drawWideLine(ix + 22, iy + iceH - 12, ix + 78,  iy + 12, 5.0f, colFrostRim);
        canvas.drawWideLine(ix + 46, iy + iceH - 12, ix + 102, iy + 12, 2.2f, colWhite);

        // Nhũ băng nhọn rủ xuống từ mép trên tảng băng
        const int spikes[5][2] = {{18, 17}, {34, 11}, {128, 14}, {146, 20}, {156, 11}};
        for (int s = 0; s < 5; s++) {
          int ox = ix + spikes[s][0];
          canvas.fillTriangle(ox - 5, iy + 2, ox + 5, iy + 2, ox, iy + 2 + spikes[s][1], colFrostRim);
        }
      }

      // Vết nứt răng cưa lan ra khi Pet gồng phá băng!
      if (crackPhase > 0.0f) {
        int rcx = 120 + (int)blockRattle;
        canvas.drawWideLine(rcx, 124, rcx - 18, 104, 2.2f, colWhite);
        canvas.drawWideLine(rcx - 18, 104, rcx - 36, 98, 2.0f, colWhite);
        if (crackPhase > 0.35f) {
          canvas.drawWideLine(rcx, 124, rcx + 24, 110, 2.2f, colWhite);
          canvas.drawWideLine(rcx + 24, 110, rcx + 44, 88, 2.0f, colWhite);
          canvas.drawWideLine(rcx, 124, rcx + 14, 154, 2.2f, colWhite);
          canvas.drawWideLine(rcx + 14, 154, rcx + 32, 174, 2.0f, colWhite);
        }
        if (crackPhase > 0.65f) {
          canvas.drawWideLine(rcx - 18, 104, rcx - 28, 148, 2.0f, colWhite);
        }
      }
    }
  } else {
    // Pha 3 & 4 (3.55s -> 5.00s): PHÁ BĂNG VỠ TUNG TÓE thành 12 mảnh băng văng tứ phía!
    if (!pet.shatterPlayed) {
      pet.shatterPlayed = true;
      if (ENABLE_SOUND && PIN_BUZZER >= 0) {
        tone(PIN_BUZZER, 1950, 35); delay(35);
        tone(PIN_BUZZER, 980,  40); delay(40);
        tone(PIN_BUZZER, 620,  45);
      }
    }
    float post      = (el - 3.55f) / 1.45f;
    float shakeFree = (post < 0.45f) ? sinf(post * 3.14159f * 10.0f) * 6.0f * (1.0f - post / 0.45f) : 0.0f;

    drawMochiHead(120.0f + shakeFree, 122.0f, 64.0f, 56.0f, 46.0f, 1.0f);
    drawPillEye(96  + (int)shakeFree, 120, 13, 24, colWhite);
    drawPillEye(144 + (int)shakeFree, 120, 13, 24, colWhite);

    if (post < 0.65f) {
      float sp = post / 0.65f;
      for (int i = 0; i < 12; i++) {
        float ang  = (i * 2.0f * 3.14159f) / 12.0f + 0.2f;
        float dist = 28.0f + sp * 95.0f;
        float sx   = 120.0f + cosf(ang) * dist;
        float sy   = 124.0f + sinf(ang) * dist + sp * sp * 28.0f;
        drawTriangleCrumb(sx, sy, 9.5f * (1.0f - sp * 0.4f), ang + sp * 5.0f,
                          (i % 2 == 0) ? colFrostRim : colFrostCyan);
      }
    }
  }
}

// ======================= 6.7 HÓA THÀNH BONG BÓNG NGŨ SẮC RỒI BAY LÊN TRỜI (🫧) =======================
void drawSoapBubbleSphere(int cx, int cy, int rx, int ry) {
  if (rx < 2 || ry < 2) return;
  uint16_t colSoapTint = tft.color565(8, 34, 58);
  uint16_t colSoapPink = tft.color565(244, 114, 182);

  canvas.fillEllipse(cx, cy, rx - 1, ry - 1, colSoapTint);
  canvas.drawEllipse(cx, cy, rx,     ry,     colIceBlue);
  if (rx > 5) {
    canvas.drawEllipse(cx, cy, rx - 1, ry - 1, colSleepBlue);
  }

  int rAvg = (rx + ry) / 2;
  if (rAvg > 10) {
    // Ánh cầu vồng xà phòng hồng & vàng hai bên mép bong bóng
    canvas.drawArc(cx, cy, rAvg - 2, rAvg - 4, 290, 355, colSoapPink);
    canvas.drawArc(cx, cy, rAvg - 2, rAvg - 4, 110, 165, colYellowLight);
    // Vệt phản quang trắng bóng gương góc trên trái + chấm sáng góc dưới phải
    int hr = (rAvg * 74) / 100;
    canvas.drawArc(cx, cy, hr, hr - 2, 198, 254, colWhite);
    int dotR = (rAvg > 30) ? 3 : 1;
    canvas.fillSmoothCircle(cx + (rx * 48) / 100, cy + (ry * 48) / 100, dotR, colWhite);
  } else {
    canvas.drawPixel(cx - rx / 2, cy - ry / 2, colWhite);
  }
}

void drawBubbleScene(float el, float t) {
  // Pha 4 (4.00s -> 4.60s): Pet bật nảy êm ái xuất hiện lại giữa màn hình
  if (el >= 4.00f) {
    float rp = (el - 4.00f) / 0.55f;
    if (rp > 1.0f) rp = 1.0f;
    float p1 = rp - 1.0f;
    float easeBack = 1.0f + 2.0f * p1 * p1 * p1 + 1.0f * p1 * p1;
    float sc = 0.25f + 0.75f * easeBack;
    if (sc < 0.1f) sc = 0.1f;
    float yOff = (1.0f - easeBack) * 24.0f;

    drawMochiHead(120.0f, 122.0f + yOff, 64.0f, 56.0f, 46.0f, sc);
    drawPillEye(120 - (int)(24.0f * sc), 120 + (int)yOff, (int)(13.0f * sc), (int)(24.0f * sc), colWhite);
    drawPillEye(120 + (int)(24.0f * sc), 120 + (int)yOff, (int)(13.0f * sc), (int)(24.0f * sc), colWhite);
    return;
  }

  // Các bong bóng nhỏ ngũ sắc bay lơ lửng theo sau!
  if (el > 0.45f && el < 3.95f) {
    for (int i = 0; i < 9; i++) {
      float bp = fmodf(el * 0.52f + i * 0.14f, 1.0f);
      int   bx = 34 + ((i * 23) % 172) + (int)(sinf(bp * 6.0f + i * 1.7f) * 12.0f);
      int   by = 225 - (int)(bp * 245.0f);
      int   br = 4 + (i % 3) * 4;
      if (by > -15 && by < 245) {
        drawSoapBubbleSphere(bx, by, br, br);
      }
    }
  }

  // Pha 1 (0 -> 0.85s): Phồng má hóa thành quả bong bóng tròn xoe
  // Pha 2-3 (0.85 -> 3.85s): Đung đưa nhẹ nhàng theo gió rồi bay bổng vút lên trời!
  float morphP = (el < 0.85f) ? (el / 0.85f) : 1.0f;
  float floatP = (el > 0.85f) ? ((el - 0.85f) / 2.85f) : 0.0f;
  float liftY  = (floatP > 0.0f) ? (powf(floatP, 1.45f) * 215.0f) : (sinf(morphP * 3.14159f) * -4.0f);
  float swayK  = (floatP * 2.2f < 1.0f) ? (floatP * 2.2f) : 1.0f;
  float swayX  = (el > 0.70f) ? (sinf((el - 0.70f) * 4.2f) * 20.0f * swayK) : 0.0f;

  float cx = 120.0f + swayX;
  float cy = 124.0f - liftY;
  float wobble = sinf(t * 7.5f) * 2.8f * morphP;
  int   rx = (int)(64.0f * (1.0f - morphP) + 56.0f * morphP + wobble);
  int   ry = (int)(51.0f * (1.0f - morphP) + 56.0f * morphP - wobble);

  if (cy > -68.0f) {
    if (morphP < 0.55f) {
      drawMochiHead(cx, cy, 64.0f, 56.0f, 46.0f, 1.0f);
    } else {
      drawSoapBubbleSphere((int)cx, (int)cy, rx, ry);
    }

    // Khuôn mặt Pet bên trong quả bong bóng đang bay lên
    if (el < 1.35f) {
      // Mắt tròn ngạc nhiên thích thú + miệng chu chữ "o" thổi hơi
      canvas.fillSmoothCircle((int)cx - 21, (int)cy - 2, 7, colWhite);
      canvas.fillSmoothCircle((int)cx + 21, (int)cy - 2, 7, colWhite);
      canvas.fillSmoothCircle((int)cx - 19, (int)cy - 4, 2, colSkyBlue);
      canvas.fillSmoothCircle((int)cx + 23, (int)cy - 4, 2, colSkyBlue);
      canvas.drawCircle((int)cx, (int)cy + 14, 4, colWhite);
      canvas.drawCircle((int)cx, (int)cy + 14, 3, colWhite);
    } else {
      // Mắt cười tít ⌒ ⌒ + miệng cười nhỏ tận hưởng cảm giác lơ lửng trên không!
      float tilt = cosf((el - 0.70f) * 4.2f) * 0.12f;
      drawUpwardArcEye(cx, cy, -20.0f, 1.0f, 9.0f, 3.8f, tilt, colWhite);
      drawUpwardArcEye(cx, cy,  20.0f, 1.0f, 9.0f, 3.8f, tilt, colWhite);
      drawDownwardSleepEye(cx, cy + 8.0f, 7.0f, 3.0f, colWhite);
    }
  }

  // Ánh sao lấp lánh trên bầu trời khi bong bóng bay khuất lên cao
  if (el > 3.15f && el < 4.00f) {
    float sp = (el - 3.15f) / 0.85f;
    float tw = sinf(sp * 3.14159f);
    if (tw > 0.1f) {
      drawSmallStar(96.0f,  32.0f, 7.5f * tw, tft.color565(244, 114, 182));
      drawSmallStar(146.0f, 24.0f, 9.0f * tw, colYellowLight);
      drawSmallStar(120.0f, 44.0f, 6.5f * tw, colIceBlue);
    }
  }
}

// ======================= 6.8 QUÁ TẢI NỔ NÃO ĐÁM MÂY HÌNH NẤM (🤯 MIND-BLOWN) =======================
void drawMindBlownScene(float el, float t) {
  uint16_t colCoral  = tft.color565(244, 63, 94);
  uint16_t colOrange = tft.color565(251, 146, 60);
  uint16_t colPink   = tft.color565(244, 114, 182);
  uint16_t colGreen  = tft.color565(74, 222, 128);

  if (el < 1.15f) {
    // Pha 1 (0.00 -> 1.15s): Mắt trợn tròn (⊙ ⊙), chóp đầu phồng căng rung bần bật sắp nổ!
    float p      = el / 1.15f;
    float shakeX = sinf(t * 68.0f) * (1.0f + p * 5.5f);
    float shakeY = cosf(t * 54.0f) * (p * 3.0f);
    float cx     = 120.0f + shakeX;
    float cy     = 132.0f + shakeY;

    float bulgeTop = 56.0f + p * p * 22.0f;
    drawMochiHead(cx, cy, 64.0f + p * 6.0f, bulgeTop, 44.0f, 1.0f);

    int er = 12 + (int)(p * 2.0f);
    canvas.fillSmoothCircle((int)cx - 24, (int)cy + 2, er, colWhite);
    canvas.fillSmoothCircle((int)cx + 24, (int)cy + 2, er, colWhite);
    canvas.fillSmoothCircle((int)(cx - 24.0f + shakeX * 0.25f), (int)cy + 2, 3, colBg);
    canvas.fillSmoothCircle((int)(cx + 24.0f + shakeX * 0.25f), (int)cy + 2, 3, colBg);

    canvas.drawEllipse((int)cx, (int)cy + 23, 7 + (int)(p * 3.0f), 6 + (int)(p * 4.0f), colWhite);
    canvas.drawEllipse((int)cx, (int)cy + 23, 6 + (int)(p * 3.0f), 5 + (int)(p * 4.0f), colWhite);

    if (p > 0.45f) {
      drawSmallStar(cx - 34.0f, cy - bulgeTop + 8.0f,  7.0f * p, colYellowLight);
      drawSmallStar(cx + 36.0f, cy - bulgeTop + 12.0f, 8.0f * p, colOrange);
    }
  } else if (el < 4.15f) {
    // Pha 2 & 3 (1.15 -> 4.15s): BÙM!! Bay nắp chóp đầu + Đám mây nấm khổng lồ phụt lên (🤯)!
    if (!pet.boomPlayed) {
      pet.boomPlayed = true;
      if (ENABLE_SOUND && PIN_BUZZER >= 0) {
        tone(PIN_BUZZER, 240, 50); delay(50);
        tone(PIN_BUZZER, 120, 65); delay(65);
        tone(PIN_BUZZER, 65,  90);
      }
    }
    float boomEl  = el - 1.15f;
    float fadeOut = (boomEl > 2.25f) ? (1.0f - (boomEl - 2.25f) / 0.75f) : 1.0f;
    if (fadeOut < 0.0f) fadeOut = 0.0f;

    float recoil = (boomEl < 0.35f) ? (sinf((boomEl / 0.35f) * 3.14159f) * 8.0f) : 0.0f;
    float shakeX = (boomEl < 0.50f) ? (sinf(t * 60.0f) * 5.0f * (1.0f - boomEl / 0.50f)) : 0.0f;
    float cx     = 120.0f + shakeX;
    float cy     = 148.0f + recoil;
    int   cutY   = (int)cy - 18;

    // Vòng sóng xung kích nổ ban đầu
    if (boomEl < 0.32f) {
      float fp = boomEl / 0.32f;
      canvas.drawCircle(120, cutY - 8, 18 + (int)(fp * 92.0f), colYellowLight);
    }

    // 1) Cột lửa & Đám mây hình nấm khổng lồ mọc thẳng từ đỉnh đầu rỗng!
    if (fadeOut > 0.12f) {
      float riseP   = (boomEl < 0.42f) ? (boomEl / 0.42f) : 1.0f;
      float driftUp = (boomEl > 0.42f) ? ((boomEl - 0.42f) * 10.0f) : 0.0f;
      int   capY    = (cutY - 8) - (int)(riseP * 68.0f + driftUp * 0.45f);

      // Thân nấm phụt lên từ đầu
      canvas.fillTriangle((int)cx - 20, cutY + 6, (int)cx + 20, cutY + 6, (int)cx, capY, colOrange);
      canvas.fillRect((int)cx - 12, capY + 8, 24, (cutY + 6) - (capY + 8), colYellow);

      if (riseP > 0.5f) {
        canvas.drawEllipse((int)cx, (cutY + capY) / 2 + 2, (int)(21.0f * riseP), 5, colYellowLight);
      }

      // Mũ nấm 3 tầng màu rực rỡ (Đỏ cam -> Cam vàng -> Trắng sáng)
      float capSc    = (0.35f + 0.65f * riseP) * fadeOut;
      int   puffRoll = (int)(sinf(t * 8.0f) * 2.0f);

      const int outerP[5][3] = {{-38, 8, 22}, {38, 8, 22}, {-23, -10, 24}, {23, -10, 24}, {0, -15, 27}};
      for (int i = 0; i < 5; i++) {
        canvas.fillSmoothCircle((int)(cx + outerP[i][0] * capSc),
                                capY + (int)(outerP[i][1] * capSc) + puffRoll,
                                (int)((outerP[i][2] + 2) * capSc), colCoral);
      }
      for (int i = 0; i < 5; i++) {
        canvas.fillSmoothCircle((int)(cx + outerP[i][0] * 0.86f * capSc),
                                capY + (int)((outerP[i][1] + 2) * capSc),
                                (int)(outerP[i][2] * 0.88f * capSc), colOrange);
      }

      const int innerP[5][3] = {{-20, 4, 15}, {20, 4, 15}, {-11, -7, 17}, {11, -7, 17}, {0, 0, 19}};
      for (int i = 0; i < 5; i++) {
        canvas.fillSmoothCircle((int)(cx + innerP[i][0] * capSc),
                                capY + (int)(innerP[i][1] * capSc),
                                (int)(innerP[i][2] * capSc), colYellowLight);
      }
      canvas.fillSmoothCircle((int)cx, capY - 2, (int)(12.0f * capSc), colWhite);

      // Mảnh pháo giấy & sao nhiều màu bắn tung ra hai bên!
      float shardP = (boomEl < 1.6f) ? (boomEl / 1.6f) : 1.0f;
      const uint16_t sCols[4] = {colSleepBlue, colYellow, colPink, colGreen};
      for (int i = 0; i < 8; i++) {
        float ang  = -3.14159f * 0.92f + ((float)i / 7.0f) * 3.14159f * 0.84f;
        float dist = 36.0f + shardP * 68.0f;
        float sx   = cx + cosf(ang) * dist;
        float sy   = (float)(cutY - 12) + sinf(ang) * dist * 0.75f;
        if (i % 2 == 0) {
          drawSmallStar(sx, sy, 6.5f * fadeOut, sCols[i % 4]);
        } else {
          drawTriangleCrumb(sx, sy, 5.5f * fadeOut, t * 4.0f + i, sCols[i % 4]);
        }
      }
    }

    // 2) Phần dưới đầu Pet bị mất chóp trên (minLocalY = -18.0f) + miệng tròn há hốc 🤯!
    drawMochiHead(cx, cy, 66.0f, 56.0f, 44.0f, 1.0f, 0.0f, 999.0f, -18.0f);
    canvas.drawEllipse((int)cx, cutY, 54, 6, (fadeOut > 0.3f) ? colOrange : colIceBlue);

    canvas.fillSmoothCircle((int)cx - 24, (int)cy + 4, 11, colWhite);
    canvas.fillSmoothCircle((int)cx + 24, (int)cy + 4, 11, colWhite);
    int pj = (int)(sinf(t * 35.0f) * 1.5f);
    canvas.fillSmoothCircle((int)cx - 24 + pj, (int)cy + 4, 3, colBg);
    canvas.fillSmoothCircle((int)cx + 24 - pj, (int)cy + 4, 3, colBg);

    canvas.drawEllipse((int)cx, (int)cy + 23, 9, 7, colWhite);
    canvas.drawEllipse((int)cx, (int)cy + 23, 8, 6, colWhite);
  } else {
    // Pha 4 (4.15 -> 4.80s): Mọc lại nắp chóp đầu tròn trịa như cũ!
    float rp = (el - 4.15f) / 0.60f;
    if (rp > 1.0f) rp = 1.0f;
    float p1 = rp - 1.0f;
    float easeBack = 1.0f + 2.0f * p1 * p1 * p1 + 1.0f * p1 * p1;
    if (easeBack < 0.4f) easeBack = 0.4f;
    float cy = 148.0f * (1.0f - rp) + 122.0f * rp;

    drawMochiHead(120.0f, cy, 64.0f, 56.0f * easeBack, 46.0f, 1.0f);
    drawPillEye(96,  (int)cy - 2, 13, 24, colWhite);
    drawPillEye(144, (int)cy - 2, 13, 24, colWhite);
  }
}

// ======================= 6.9 CHÚC MỪNG SINH NHẬT (🎂 BÁNH KEM THỔI NẾN + NHẠC HAPPY BIRTHDAY) =======================
struct BdayNote {
  float    timeSec;
  uint16_t freqHz;
  uint16_t durMs;
};

const BdayNote BDAY_MELODY[] = {
  {0.00f, 392, 170}, {0.22f, 392, 170}, {0.44f, 440, 300}, {0.80f, 392, 300}, {1.16f, 523, 300}, {1.52f, 494, 480},
  {2.15f, 392, 170}, {2.37f, 392, 170}, {2.59f, 440, 300}, {2.95f, 392, 300}, {3.31f, 587, 300}, {3.67f, 523, 520}
};
const int BDAY_MELODY_LEN = sizeof(BDAY_MELODY) / sizeof(BDAY_MELODY[0]);

void drawBirthdayScene(float el, float t) {
  // Phát nhạc "Happy Birthday to You" theo thời gian thực mượt 60 FPS (không dùng delay!)
  if (ENABLE_SOUND && PIN_BUZZER >= 0 && pet.bdayNoteIdx < BDAY_MELODY_LEN) {
    if (el >= BDAY_MELODY[pet.bdayNoteIdx].timeSec) {
      tone(PIN_BUZZER, BDAY_MELODY[pet.bdayNoteIdx].freqHz, BDAY_MELODY[pet.bdayNoteIdx].durMs);
      pet.bdayNoteIdx++;
    }
  }

  // Pha cuối (5.55 -> 6.20s): Trở lại giữa màn hình mượt mà
  if (el >= 5.55f) {
    float rp = (el - 5.55f) / 0.60f;
    if (rp > 1.0f) rp = 1.0f;
    float sc = 0.58f + 0.42f * rp;
    float cy = 108.0f * (1.0f - rp) + 122.0f * rp;
    drawMochiHead(120.0f, cy, 64.0f, 56.0f, 46.0f, sc);
    drawPillEye(120 - (int)(24.0f * sc), (int)cy - 2, (int)(13.0f * sc), (int)(24.0f * sc), colWhite);
    drawPillEye(120 + (int)(24.0f * sc), (int)cy - 2, (int)(13.0f * sc), (int)(24.0f * sc), colWhite);
    return;
  }

  uint16_t colCakePink  = tft.color565(251, 113, 133);
  uint16_t colPartyHat  = tft.color565(236, 72, 153);
  uint16_t colCream     = tft.color565(255, 251, 235);
  uint16_t colOrange    = tft.color565(249, 115, 22);
  uint16_t colMint      = tft.color565(74, 222, 128);
  uint16_t colPlate     = tft.color565(226, 232, 240);

  bool  candleBlownOut = (el >= 3.25f);
  bool  isBlowing      = (el >= 2.75f && el < 3.35f);
  float cheerHop       = candleBlownOut ? (fabsf(sinf((el - 3.25f) * 8.5f)) * 9.0f) : 0.0f;
  float swayX          = !isBlowing ? (sinf(t * 5.2f) * 5.0f) : 0.0f;

  // 1) Pháo giấy tam giác nhiều màu rơi tưng bừng!
  const uint16_t confCols[4] = {colPartyHat, colYellow, colSleepBlue, colMint};
  for (int i = 0; i < 10; i++) {
    float cp = fmodf(el * 0.45f + i * 0.13f, 1.0f);
    float kx = 18.0f + ((i * 23) % 204) + sinf(cp * 7.0f + i) * 8.0f;
    float ky = 10.0f + cp * 218.0f;
    drawTriangleCrumb(kx, ky, 4.5f, t * 4.0f + i, confCols[i % 4]);
  }

  // 2) Dòng chữ "HAPPY BIRTHDAY!" vàng rực rỡ phía trên!
  int titleBob = (int)(sinf(t * 6.0f) * 2.0f);
  canvas.setFont(&fonts::FreeSansBold9pt7b);
  int tw = canvas.textWidth("HAPPY BIRTHDAY!");
  canvas.setTextColor(colPartyHat);
  canvas.setCursor(120 - tw / 2 + 1, 17 + titleBob);
  canvas.print("HAPPY BIRTHDAY!");
  canvas.setTextColor(colYellowLight);
  canvas.setCursor(120 - tw / 2, 16 + titleBob);
  canvas.print("HAPPY BIRTHDAY!");

  // 3) Pháo hoa chúc mừng nổ rực rỡ sau khi thổi tắt nến!
  if (candleBlownOut) {
    float fwTime = el - 3.25f;
    const int fwPos[3][2]     = {{50, 68}, {190, 64}, {62, 92}};
    const float fwDelay[3]    = {0.0f, 0.45f, 0.95f};
    const uint16_t fwCols[3]  = {colPartyHat, colYellowLight, colSleepBlue};
    for (int f = 0; f < 3; f++) {
      if (fwTime >= fwDelay[f]) {
        float local = fmodf(fwTime - fwDelay[f], 1.35f);
        if (local < 0.85f) {
          float fp = local / 0.85f;
          for (int k = 0; k < 8; k++) {
            float a    = (k * 2.0f * 3.14159f) / 8.0f;
            float dist = 6.0f + fp * 26.0f;
            canvas.fillSmoothCircle(fwPos[f][0] + (int)(cosf(a) * dist),
                                    fwPos[f][1] + (int)(sinf(a) * dist), 2, fwCols[f]);
          }
        }
      }
    }
  }

  // 4) Bé Pet đội mũ chóp sinh nhật (🥳) đung đưa phía sau bánh kem!
  float petCx = 120.0f + swayX;
  float petCy = 106.0f - cheerHop + (isBlowing ? 4.0f : 0.0f);
  float petSc = 0.56f;

  drawMochiHead(petCx, petCy, 64.0f, 56.0f, 46.0f, petSc);

  int hatBaseY = (int)petCy - 29;
  int pcx      = (int)petCx;
  canvas.fillTriangle(pcx - 16, hatBaseY, pcx + 16, hatBaseY, pcx, hatBaseY - 30, colPartyHat);
  canvas.drawWideLine(pcx - 9, hatBaseY - 6, pcx + 7, hatBaseY - 14, 2.8f, colYellowLight);
  canvas.fillSmoothCircle(pcx, hatBaseY - 32, 5, colYellowLight);

  if (isBlowing) {
    // Nheo mắt > < chu miệng thổi hơi tắt nến!
    canvas.drawWideLine(petCx - 18, petCy - 4, petCx - 10, petCy,     3.2f, colWhite);
    canvas.drawWideLine(petCx - 10, petCy,     petCx - 18, petCy + 4, 3.2f, colWhite);
    canvas.drawWideLine(petCx + 18, petCy - 4, petCx + 10, petCy,     3.2f, colWhite);
    canvas.drawWideLine(petCx + 10, petCy,     petCx + 18, petCy + 4, 3.2f, colWhite);
    canvas.fillSmoothCircle(pcx, (int)petCy + 11, 4, colWhite);

    float blowP = (el - 2.75f) / 0.60f;
    canvas.drawWideLine(petCx - 3, petCy + 16, petCx - 10, petCy + 20 + blowP * 16.0f, 2.0f, colIceBlue);
    canvas.drawWideLine(petCx,     petCy + 16, petCx,      petCy + 24 + blowP * 16.0f, 2.0f, colIceBlue);
    canvas.drawWideLine(petCx + 3, petCy + 16, petCx + 10, petCy + 20 + blowP * 16.0f, 2.0f, colIceBlue);
  } else {
    drawUpwardArcEye(petCx, petCy, -14.0f, 3.0f, 6.8f, 3.4f, 0.0f, colWhite);
    drawUpwardArcEye(petCx, petCy,  14.0f, 3.0f, 6.8f, 3.4f, 0.0f, colWhite);
    if (candleBlownOut) {
      drawDownwardSleepEye(petCx, petCy + 6.0f, 8.0f, 3.2f, colWhite);
    }
  }

  // 5) Bánh kem dâu 2 tầng + Nến sinh nhật lung linh phía trước!
  int cakeY = 166;
  int cakeW = 98;
  int cakeH = 34;
  int cakeX = 120 - cakeW / 2;

  canvas.fillEllipse(120, cakeY + cakeH + 1, 60, 6, colPlate);
  canvas.fillSmoothRoundRect(cakeX, cakeY, cakeW, cakeH, 8, colCakePink);
  canvas.fillRect(cakeX + 2, cakeY + 17, cakeW - 4, 5, colYellowLight);

  // Lớp kem phủ trắng chảy viền tròn trên mặt bánh
  canvas.fillSmoothRoundRect(cakeX - 1, cakeY - 2, cakeW + 2, 12, 6, colCream);
  for (int i = 0; i < 6; i++) {
    canvas.fillSmoothCircle(cakeX + 9 + i * 16, cakeY + 9, 6, colCream);
  }

  // Cây nến sọc đỏ trắng ở chính giữa bánh
  int candleX    = 120;
  int candleTopY = cakeY - 20;
  canvas.fillRect(candleX - 4, candleTopY, 8, 19, colWhite);
  canvas.drawWideLine(candleX - 4, candleTopY + 6,  candleX + 4, candleTopY + 3,  2.0f, colCakePink);
  canvas.drawWideLine(candleX - 4, candleTopY + 14, candleX + 4, candleTopY + 11, 2.0f, colCakePink);
  canvas.drawLine(candleX, candleTopY, candleX, candleTopY - 4, colPlate);

  if (!candleBlownOut) {
    // Ngọn lửa nến lung linh (bị thổi nghiêng khi Pet thổi hơi!)
    float flicker = isBlowing ? (sinf(t * 50.0f) * 5.0f) : (sinf(t * 18.0f) * 1.8f);
    int   fx      = candleX + (int)flicker;
    int   fy      = candleTopY - 12 + (isBlowing ? 4 : 0);

    canvas.fillTriangle(candleX - 5, candleTopY - 5, candleX + 5, candleTopY - 5, fx, fy - 9, colOrange);
    canvas.fillSmoothCircle(fx, fy, 6, colOrange);
    canvas.fillSmoothCircle(fx, fy + 1, 3, colYellowLight);
  } else {
    // Làn khói trắng uốn lượn bay lên từ tim nến vừa tắt!
    float smP = (el - 3.25f) / 1.4f;
    if (smP < 1.0f) {
      int sy = candleTopY - 5 - (int)(smP * 22.0f);
      canvas.drawWideLine(candleX,     sy + 10, candleX - 4, sy + 3,  2.0f, colPlate);
      canvas.drawWideLine(candleX - 4, sy + 3,  candleX + 4, sy - 4,  2.0f, colPlate);
      canvas.drawWideLine(candleX + 4, sy - 4,  candleX - 2, sy - 11, 2.0f, colPlate);
    }
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
      float dur = (next == MODE_IDLE) ? 999.0f
                : ((next == MODE_MELT || next == MODE_FREEZE) ? 5.0f
                : ((next == MODE_MINDBLOWN) ? 4.8f : ((next == MODE_BUBBLE) ? 4.6f : 2.6f)));
      setPetMode(next, dur, true, true);
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
  charSpr.setColorDepth(canvas.getColorDepth());
  charSpr.createSprite(24, 28);
  charSpr.setPivot(12, 14);

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
        float dur = (nextStory == MODE_BIRTHDAY) ? 6.2f
                  : ((nextStory == MODE_MELT || nextStory == MODE_FREEZE) ? 5.0f
                  : ((nextStory == MODE_MINDBLOWN) ? 4.8f
                  : ((nextStory == MODE_BUBBLE) ? 4.6f : ((nextStory == MODE_FORTUNE) ? 4.3f : 3.9f))));
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
    case MODE_MELT:
      drawMeltScene(pet.animElapsed, t);
      break;
    case MODE_FREEZE:
      drawFreezeScene(pet.animElapsed, t);
      break;
    case MODE_BUBBLE:
      drawBubbleScene(pet.animElapsed, t);
      break;
    case MODE_MINDBLOWN:
      drawMindBlownScene(pet.animElapsed, t);
      break;
    case MODE_BIRTHDAY:
      drawBirthdayScene(pet.animElapsed, t);
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
