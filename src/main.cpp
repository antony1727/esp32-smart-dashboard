#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include <ArduinoJson.h>
#include <lvgl.h>
#include "LGFX_ESP32_8048S070.h"
#include "app_config.h"
#include "web_server.h"
#include "ota_updater.h"
#include "version.h"

LGFX tft;
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf1 = nullptr;
static lv_color_t *buf2 = nullptr;
#define BUF_LINES 32 // 800*32 = 25600 px ~51KB (IRAM)

// UI Widgets - Left Panel (Painel Unificado)
static lv_obj_t *time_label = nullptr;
static lv_obj_t *date_label = nullptr;
static lv_obj_t *weather_temp_label = nullptr;
static lv_obj_t *weather_city_label = nullptr;
static lv_obj_t *weather_icon_box = nullptr;

// UI Widgets - Weekly Forecast (6 Colunas)
static lv_obj_t *forecast_day_labels[6] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
static lv_obj_t *forecast_icon_boxes[6] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
static lv_obj_t *forecast_temp_labels[6] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};

// UI Widgets - Right Panel (4 Cards de Moedas com Sparklines)
static lv_obj_t *moeda_cards[4] = {nullptr, nullptr, nullptr, nullptr};
static lv_obj_t *moeda_pair_labels[4] = {nullptr, nullptr, nullptr, nullptr};
static lv_obj_t *moeda_sub_labels[4] = {nullptr, nullptr, nullptr, nullptr};
static lv_obj_t *moeda_value_labels[4] = {nullptr, nullptr, nullptr, nullptr};
static lv_obj_t *moeda_pct_labels[4] = {nullptr, nullptr, nullptr, nullptr};
static lv_obj_t *moeda_icon_boxes[4] = {nullptr, nullptr, nullptr, nullptr};
static lv_obj_t *moeda_lines[4] = {nullptr, nullptr, nullptr, nullptr};
static lv_point_t moeda_spark_pts[4][7];

// UI Widgets - Rodapé
static lv_obj_t *bottom_wifi_label = nullptr;
static lv_obj_t *bottom_version_label = nullptr;
static lv_obj_t *wifi_bars[4] = {nullptr, nullptr, nullptr, nullptr};

// Variáveis de Estado
static String moedaValues[6] = {"R$ 404.208", "R$ 5,16", "R$ 14.500", "R$ 5,60", "R$ --,--", "R$ --,--"};
static String moedaPcts[6] = {"v -1,01%", "^ +0,26%", "v -0,55%", "^ +0,10%", "--", "--"};
static bool moedaPctPos[6] = {false, true, false, true, true, true};
String dolarValue = "R$ 5,16";
String weatherTemp = "25 C";
String weatherDesc = "Chuva";
String weatherCity = "NEPOMUCENO";
int currentWeatherCode = 61; // Chuva realista de referência

// Dados da Previsão Semanal (6 Dias Exatos da Imagem)
struct DayForecast {
  String dayName;
  int tempVal;
  bool isMin;
  int weatherCode;
};
static DayForecast weeklyForecast[6] = {
  {"QUI", 26, false, 1},
  {"SEX", 27, false, 1},
  {"SAB", 16, false, 61},
  {"DOM", 23, false, 61},
  {"SEG", 15, false, 1},
  {"SEG", 16, true,  1}
};

volatile bool gNeedsRebuild = false;

// Remove acentos para compatibilidade total com fontes ASCII do LVGL
String sanitize_for_lvgl(String str) {
  String s = str;
  s.replace("á", "a"); s.replace("à", "a"); s.replace("ã", "a"); s.replace("â", "a"); s.replace("ä", "a");
  s.replace("Á", "A"); s.replace("À", "A"); s.replace("Ã", "A"); s.replace("Â", "A"); s.replace("Ä", "A");
  s.replace("é", "e"); s.replace("ê", "e"); s.replace("è", "e"); s.replace("ë", "e");
  s.replace("É", "E"); s.replace("Ê", "E"); s.replace("È", "E"); s.replace("Ë", "E");
  s.replace("í", "i"); s.replace("ì", "i"); s.replace("î", "i"); s.replace("ï", "i");
  s.replace("Í", "I"); s.replace("Ì", "I"); s.replace("Î", "I"); s.replace("Ï", "I");
  s.replace("ó", "o"); s.replace("õ", "o"); s.replace("ô", "o"); s.replace("ò", "o"); s.replace("ö", "o");
  s.replace("Ó", "O"); s.replace("Õ", "O"); s.replace("Ô", "O"); s.replace("Ò", "O"); s.replace("Ö", "O");
  s.replace("ú", "u"); s.replace("ù", "u"); s.replace("û", "u"); s.replace("ü", "u");
  s.replace("Ú", "U"); s.replace("Ù", "U"); s.replace("Û", "U"); s.replace("Ü", "U");
  s.replace("ç", "c"); s.replace("Ç", "C");
  s.replace("º", "");  s.replace("ª", "");
  return s;
}

void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);
  tft.pushImage(area->x1, area->y1, w, h, (lgfx::rgb565_t *)&color_p->full);
  lv_disp_flush_ready(disp);
}

void my_touch_read(lv_indev_drv_t *indev, lv_indev_data_t *data) {
  uint16_t x, y;
  if (tft.getTouch(&x, &y)) {
    data->state = LV_INDEV_STATE_PRESSED;
    data->point.x = x;
    data->point.y = y;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

// Nomes amigáveis das moedas
const char* get_currency_friendly_name(const char* pair) {
  if (strstr(pair, "BTC")) return "Bitcoin";
  if (strstr(pair, "USD")) return "Dolar";
  if (strstr(pair, "EUR")) return "Euro";
  if (strstr(pair, "ETH")) return "Ethereum";
  if (strstr(pair, "USDT")) return "Tether";
  if (strstr(pair, "GBP")) return "Libra";
  if (strstr(pair, "JPY")) return "Iene";
  if (strstr(pair, "CAD")) return "Dolar Can.";
  if (strstr(pair, "CHF")) return "Franco Suico";
  if (strstr(pair, "ARS")) return "Peso Arg.";
  if (strstr(pair, "SOL")) return "Solana";
  return "Cambio";
}

// Renderizador dos 4 Ícones Circulares Idênticos à Foto de Referência
void render_currency_icon(lv_obj_t *parent, const char* pair) {
  lv_obj_clean(parent);

  // 1. BITCOIN - Moeda Dourada / Laranja Circular com B estilizado
  if (strstr(pair, "BTC")) {
    lv_obj_t *circle = lv_obj_create(parent);
    lv_obj_set_size(circle, 44, 44);
    lv_obj_align(circle, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(circle, lv_color_hex(0xF7931A), 0);
    lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(circle, 1, 0);
    lv_obj_set_style_border_color(circle, lv_color_hex(0xFFB049), 0);
    lv_obj_set_style_pad_all(circle, 0, 0);
    lv_obj_clear_flag(circle, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *sym = lv_label_create(circle);
    lv_label_set_text(sym, "B");
    lv_obj_set_style_text_font(sym, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(sym, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(sym, LV_ALIGN_CENTER, 0, 0);
  }
  // 2. USD - Moeda Circular com Bandeira dos Estados Unidos
  else if (strstr(pair, "USD") && !strstr(pair, "USDT")) {
    lv_obj_t *circle = lv_obj_create(parent);
    lv_obj_set_size(circle, 44, 44);
    lv_obj_align(circle, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(circle, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(circle, true, 0);
    lv_obj_set_style_border_width(circle, 1, 0);
    lv_obj_set_style_border_color(circle, lv_color_hex(0x475569), 0);
    lv_obj_set_style_pad_all(circle, 0, 0);
    lv_obj_clear_flag(circle, LV_OBJ_FLAG_SCROLLABLE);

    // Listras vermelhas da bandeira americana
    for (int s = 0; s < 5; s++) {
      lv_obj_t *stripe = lv_obj_create(circle);
      lv_obj_set_size(stripe, 44, 4);
      lv_obj_set_pos(stripe, 0, s * 9);
      lv_obj_set_style_bg_color(stripe, lv_color_hex(0xDC2626), 0);
      lv_obj_set_style_radius(stripe, 0, 0);
      lv_obj_set_style_border_width(stripe, 0, 0);
      lv_obj_clear_flag(stripe, LV_OBJ_FLAG_SCROLLABLE);
    }

    // Cantão azul no canto superior esquerdo
    lv_obj_t *canton = lv_obj_create(circle);
    lv_obj_set_size(canton, 22, 22);
    lv_obj_set_pos(canton, 0, 0);
    lv_obj_set_style_bg_color(canton, lv_color_hex(0x1E3A8A), 0);
    lv_obj_set_style_radius(canton, 0, 0);
    lv_obj_set_style_border_width(canton, 0, 0);
    lv_obj_clear_flag(canton, LV_OBJ_FLAG_SCROLLABLE);

    // Pontos brancos de estrelas no cantão
    for (int r = 0; r < 2; r++) {
      for (int c = 0; c < 2; c++) {
        lv_obj_t *st = lv_obj_create(canton);
        lv_obj_set_size(st, 3, 3);
        lv_obj_set_pos(st, 4 + c * 8, 4 + r * 8);
        lv_obj_set_style_bg_color(st, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_radius(st, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(st, 0, 0);
        lv_obj_clear_flag(st, LV_OBJ_FLAG_SCROLLABLE);
      }
    }
  }
  // 3. ETH - Moeda Circular Escura com Cristal 3D Ethereum (sem transform layer para estabilidade total)
  else if (strstr(pair, "ETH")) {
    lv_obj_t *circle = lv_obj_create(parent);
    lv_obj_set_size(circle, 44, 44);
    lv_obj_align(circle, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(circle, lv_color_hex(0x212838), 0);
    lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(circle, 1, 0);
    lv_obj_set_style_border_color(circle, lv_color_hex(0x3B485E), 0);
    lv_obj_set_style_pad_all(circle, 0, 0);
    lv_obj_clear_flag(circle, LV_OBJ_FLAG_SCROLLABLE);

    // Geometria precisa do diamante Ethereum usando linhas nativas
    static const lv_point_t eth_top[] = {{22, 8}, {31, 21}, {22, 26}, {13, 21}, {22, 8}};
    static const lv_point_t eth_bot[] = {{13, 23}, {22, 36}, {31, 23}};
    static const lv_point_t eth_mid[] = {{22, 8}, {22, 26}};

    lv_obj_t *l_top = lv_line_create(circle);
    lv_line_set_points(l_top, eth_top, 5);
    lv_obj_set_style_line_color(l_top, lv_color_hex(0xE2E8F0), 0);
    lv_obj_set_style_line_width(l_top, 2, 0);

    lv_obj_t *l_bot = lv_line_create(circle);
    lv_line_set_points(l_bot, eth_bot, 3);
    lv_obj_set_style_line_color(l_bot, lv_color_hex(0x94A3B8), 0);
    lv_obj_set_style_line_width(l_bot, 2, 0);

    lv_obj_t *l_mid = lv_line_create(circle);
    lv_line_set_points(l_mid, eth_mid, 2);
    lv_obj_set_style_line_color(l_mid, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_line_width(l_mid, 1, 0);
  }
  // 4. EUR - Moeda Circular Azul Real com Círculo de Estrelas Douradas da União Europeia
  else if (strstr(pair, "EUR")) {
    lv_obj_t *circle = lv_obj_create(parent);
    lv_obj_set_size(circle, 44, 44);
    lv_obj_align(circle, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(circle, lv_color_hex(0x003399), 0);
    lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(circle, 1, 0);
    lv_obj_set_style_border_color(circle, lv_color_hex(0x2563EB), 0);
    lv_obj_set_style_pad_all(circle, 0, 0);
    lv_obj_clear_flag(circle, LV_OBJ_FLAG_SCROLLABLE);

    // 8 estrelas douradas dispostas em anel circular
    const int cx = 22;
    const int cy = 22;
    const int rad = 12;
    for (int a = 0; a < 8; a++) {
      float angle = a * (2.0f * 3.14159f / 8.0f);
      int sx = cx + (int)round(rad * cos(angle)) - 2;
      int sy = cy + (int)round(rad * sin(angle)) - 2;
      lv_obj_t *star = lv_obj_create(circle);
      lv_obj_set_size(star, 3, 3);
      lv_obj_set_pos(star, sx, sy);
      lv_obj_set_style_bg_color(star, lv_color_hex(0xFBBF24), 0);
      lv_obj_set_style_radius(star, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_border_width(star, 0, 0);
      lv_obj_clear_flag(star, LV_OBJ_FLAG_SCROLLABLE);
    }
  }
  else {
    lv_obj_t *circle = lv_obj_create(parent);
    lv_obj_set_size(circle, 44, 44);
    lv_obj_align(circle, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(circle, lv_color_hex(0x6366F1), 0);
    lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(circle, 0, 0);
    lv_obj_set_style_pad_all(circle, 0, 0);
    lv_obj_clear_flag(circle, LV_OBJ_FLAG_SCROLLABLE);

    char code[4] = {0};
    strncpy(code, pair, 3);
    lv_obj_t *sym = lv_label_create(circle);
    lv_label_set_text(sym, code);
    lv_obj_set_style_text_font(sym, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(sym, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(sym, LV_ALIGN_CENTER, 0, 0);
  }
}

// Renderizador da Nuvem Realista 3D com Gotas de Chuva (Idêntica à Imagem)
void render_weather_icon(lv_obj_t *parent, int wcode) {
  if (!parent) return;
  lv_obj_clean(parent);

  bool isSunnyOnly = (wcode == 0);
  bool isRain = (wcode >= 51 && wcode <= 67) || (wcode >= 80 && wcode <= 82) || (wcode == 61);

  // 1. Sol com brilho dourado se ensolarado
  if (isSunnyOnly) {
    lv_obj_t *sun = lv_obj_create(parent);
    lv_obj_set_size(sun, 48, 48);
    lv_obj_align(sun, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(sun, lv_color_hex(0xFBBF24), 0);
    lv_obj_set_style_radius(sun, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(sun, 0, 0);
    lv_obj_set_style_shadow_width(sun, 18, 0);
    lv_obj_set_style_shadow_color(sun, lv_color_hex(0xF59E0B), 0);
    lv_obj_set_style_shadow_opa(sun, 200, 0);
    lv_obj_clear_flag(sun, LV_OBJ_FLAG_SCROLLABLE);
    return;
  }

  // 2. Nuvem Volumétrica 3D (Camadas sombreadas com relevo realista)
  lv_obj_t *cBaseShadow = lv_obj_create(parent);
  lv_obj_set_size(cBaseShadow, 64, 26);
  lv_obj_set_pos(cBaseShadow, 8, 22);
  lv_obj_set_style_bg_color(cBaseShadow, lv_color_hex(0x475569), 0);
  lv_obj_set_style_radius(cBaseShadow, 13, 0);
  lv_obj_set_style_border_width(cBaseShadow, 0, 0);
  lv_obj_clear_flag(cBaseShadow, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *cBase = lv_obj_create(parent);
  lv_obj_set_size(cBase, 60, 24);
  lv_obj_set_pos(cBase, 10, 18);
  lv_obj_set_style_bg_color(cBase, lv_color_hex(0x94A3B8), 0);
  lv_obj_set_style_radius(cBase, 12, 0);
  lv_obj_set_style_border_width(cBase, 0, 0);
  lv_obj_clear_flag(cBase, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *cDomeL = lv_obj_create(parent);
  lv_obj_set_size(cDomeL, 26, 26);
  lv_obj_set_pos(cDomeL, 14, 10);
  lv_obj_set_style_bg_color(cDomeL, lv_color_hex(0xCBD5E1), 0);
  lv_obj_set_style_radius(cDomeL, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(cDomeL, 0, 0);
  lv_obj_clear_flag(cDomeL, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *cDomeMid = lv_obj_create(parent);
  lv_obj_set_size(cDomeMid, 32, 32);
  lv_obj_set_pos(cDomeMid, 26, 4);
  lv_obj_set_style_bg_color(cDomeMid, lv_color_hex(0xE2E8F0), 0);
  lv_obj_set_style_radius(cDomeMid, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(cDomeMid, 0, 0);
  lv_obj_clear_flag(cDomeMid, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *cDomeR = lv_obj_create(parent);
  lv_obj_set_size(cDomeR, 24, 24);
  lv_obj_set_pos(cDomeR, 44, 12);
  lv_obj_set_style_bg_color(cDomeR, lv_color_hex(0xCBD5E1), 0);
  lv_obj_set_style_radius(cDomeR, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(cDomeR, 0, 0);
  lv_obj_clear_flag(cDomeR, LV_OBJ_FLAG_SCROLLABLE);

  // Brilho no topo da nuvem
  lv_obj_t *cHighlight = lv_obj_create(parent);
  lv_obj_set_size(cHighlight, 20, 20);
  lv_obj_set_pos(cHighlight, 32, 4);
  lv_obj_set_style_bg_color(cHighlight, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_radius(cHighlight, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(cHighlight, 0, 0);
  lv_obj_clear_flag(cHighlight, LV_OBJ_FLAG_SCROLLABLE);

  // 3. Fileiras de Gotas de Chuva Caindo
  if (isRain) {
    const int dropX[6] = {16, 24, 32, 40, 48, 56};
    const int dropY[6] = {48, 54, 49, 55, 50, 53};
    const int dropH[6] = {10, 8, 11, 9, 10, 8};
    for (int d = 0; d < 6; d++) {
      lv_obj_t *drop = lv_obj_create(parent);
      lv_obj_set_size(drop, 2, dropH[d]);
      lv_obj_set_pos(drop, dropX[d], dropY[d]);
      lv_obj_set_style_bg_color(drop, lv_color_hex(0x38BDF8), 0);
      lv_obj_set_style_radius(drop, 1, 0);
      lv_obj_set_style_border_width(drop, 0, 0);
      lv_obj_clear_flag(drop, LV_OBJ_FLAG_SCROLLABLE);
    }
  }
}

// Mini Ícones de Clima para a Previsão Semanal
void render_mini_weather_icon(lv_obj_t *parent, int wcode) {
  if (!parent) return;
  lv_obj_clean(parent);

  bool isRain = (wcode >= 51 && wcode <= 67) || (wcode >= 80 && wcode <= 82) || (wcode == 61);

  if (!isRain) {
    // Sol atrás da nuvem
    lv_obj_t *sun = lv_obj_create(parent);
    lv_obj_set_size(sun, 12, 12);
    lv_obj_set_pos(sun, 14, 2);
    lv_obj_set_style_bg_color(sun, lv_color_hex(0xFBBF24), 0);
    lv_obj_set_style_radius(sun, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(sun, 0, 0);
    lv_obj_clear_flag(sun, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *cloud = lv_obj_create(parent);
    lv_obj_set_size(cloud, 22, 10);
    lv_obj_set_pos(cloud, 4, 8);
    lv_obj_set_style_bg_color(cloud, lv_color_hex(0xE2E8F0), 0);
    lv_obj_set_style_radius(cloud, 5, 0);
    lv_obj_set_style_border_width(cloud, 0, 0);
    lv_obj_clear_flag(cloud, LV_OBJ_FLAG_SCROLLABLE);
  } else {
    // Nuvem de chuva com gotas
    lv_obj_t *cloud = lv_obj_create(parent);
    lv_obj_set_size(cloud, 22, 10);
    lv_obj_set_pos(cloud, 4, 6);
    lv_obj_set_style_bg_color(cloud, lv_color_hex(0x94A3B8), 0);
    lv_obj_set_style_radius(cloud, 5, 0);
    lv_obj_set_style_border_width(cloud, 0, 0);
    lv_obj_clear_flag(cloud, LV_OBJ_FLAG_SCROLLABLE);

    for (int d = 0; d < 3; d++) {
      lv_obj_t *drop = lv_obj_create(parent);
      lv_obj_set_size(drop, 2, 5);
      lv_obj_set_pos(drop, 7 + d * 6, 18);
      lv_obj_set_style_bg_color(drop, lv_color_hex(0x38BDF8), 0);
      lv_obj_set_style_radius(drop, 1, 0);
      lv_obj_set_style_border_width(drop, 0, 0);
      lv_obj_clear_flag(drop, LV_OBJ_FLAG_SCROLLABLE);
    }
  }
}

// Atualização Dinâmica do Gráfico Sparkline de cada Moeda
void update_sparkline(int idx, bool isPos) {
  if (idx < 0 || idx >= 4) return;
  if (isPos) {
    moeda_spark_pts[idx][0] = (lv_point_t){0, 14};
    moeda_spark_pts[idx][1] = (lv_point_t){12, 12};
    moeda_spark_pts[idx][2] = (lv_point_t){24, 7};
    moeda_spark_pts[idx][3] = (lv_point_t){36, 10};
    moeda_spark_pts[idx][4] = (lv_point_t){48, 5};
    moeda_spark_pts[idx][5] = (lv_point_t){60, 6};
    moeda_spark_pts[idx][6] = (lv_point_t){70, 1};
  } else {
    moeda_spark_pts[idx][0] = (lv_point_t){0, 2};
    moeda_spark_pts[idx][1] = (lv_point_t){12, 5};
    moeda_spark_pts[idx][2] = (lv_point_t){24, 3};
    moeda_spark_pts[idx][3] = (lv_point_t){36, 12};
    moeda_spark_pts[idx][4] = (lv_point_t){48, 8};
    moeda_spark_pts[idx][5] = (lv_point_t){60, 14};
    moeda_spark_pts[idx][6] = (lv_point_t){70, 15};
  }
  if (moeda_lines[idx]) {
    lv_line_set_points(moeda_lines[idx], moeda_spark_pts[idx], 7);
    lv_obj_set_style_line_color(moeda_lines[idx], isPos ? lv_color_hex(0x22C55E) : lv_color_hex(0xEF4444), 0);
  }
}

// Criação da Interface Completa Idêntica à Foto
void create_ui() {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_clean(scr);

  // Reset de todos os ponteiros de widgets para evitar dangling pointers
  time_label = nullptr;
  date_label = nullptr;
  weather_temp_label = nullptr;
  weather_city_label = nullptr;
  weather_icon_box = nullptr;
  bottom_wifi_label = nullptr;
  bottom_version_label = nullptr;
  for (int i = 0; i < 4; i++) {
    moeda_cards[i] = nullptr;
    moeda_pair_labels[i] = nullptr;
    moeda_sub_labels[i] = nullptr;
    moeda_value_labels[i] = nullptr;
    moeda_pct_labels[i] = nullptr;
    moeda_icon_boxes[i] = nullptr;
    moeda_lines[i] = nullptr;
    wifi_bars[i] = nullptr;
  }
  for (int i = 0; i < 6; i++) {
    forecast_day_labels[i] = nullptr;
    forecast_icon_boxes[i] = nullptr;
    forecast_temp_labels[i] = nullptr;
  }

  // Fundo Preto Profundo / Automotivo do Painel
  lv_color_t colBg = lv_color_hex(0x0A0E17);
  lv_color_t colCard = lv_color_hex(0x101522);
  lv_color_t colSubCard = lv_color_hex(0x090D15);
  lv_color_t colBorder = lv_color_hex(0x232E42);
  lv_color_t colWhite = lv_color_hex(0xFFFFFF);
  lv_color_t colMuted = lv_color_hex(0x8E9CB2);

  lv_obj_set_style_bg_color(scr, colBg, 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

  // Linhas diagonais decorativas sutis no fundo (estilo fibra de carbono)
  static lv_point_t diagPts1[2] = {{0, 60}, {60, 0}};
  static lv_point_t diagPts2[2] = {{0, 120}, {120, 0}};
  lv_obj_t *dLine1 = lv_line_create(scr);
  lv_line_set_points(dLine1, diagPts1, 2);
  lv_obj_set_style_line_color(dLine1, lv_color_hex(0x161E2E), 0);
  lv_obj_set_style_line_width(dLine1, 1, 0);

  lv_obj_t *dLine2 = lv_line_create(scr);
  lv_line_set_points(dLine2, diagPts2, 2);
  lv_obj_set_style_line_color(dLine2, lv_color_hex(0x161E2E), 0);
  lv_obj_set_style_line_width(dLine2, 1, 0);

  // ==========================================
  // PAINEL ESQUERDO: CARD INTEGRADO (366 x 430)
  // ==========================================
  lv_obj_t *left_panel = lv_obj_create(scr);
  lv_obj_set_pos(left_panel, 20, 16);
  lv_obj_set_size(left_panel, 366, 430);
  lv_obj_set_style_bg_color(left_panel, colCard, 0);
  lv_obj_set_style_bg_opa(left_panel, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(left_panel, 18, 0);
  lv_obj_set_style_border_width(left_panel, 1, 0);
  lv_obj_set_style_border_color(left_panel, colBorder, 0);
  lv_obj_set_style_pad_all(left_panel, 8, 0);
  lv_obj_clear_flag(left_panel, LV_OBJ_FLAG_SCROLLABLE);

  // 1. Data centralizada (ex: "QUA, 26 AGO")
  date_label = lv_label_create(left_panel);
  lv_label_set_text(date_label, "QUA, 26 AGO");
  lv_obj_set_style_text_font(date_label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(date_label, colMuted, 0);
  lv_obj_set_style_text_letter_space(date_label, 1, 0);
  lv_obj_align(date_label, LV_ALIGN_TOP_MID, 0, 6);

  // 2. Relógio Digital Grande centralizado (ex: "11:49")
  time_label = lv_label_create(left_panel);
  lv_label_set_text(time_label, "--:--");
  lv_obj_set_style_text_font(time_label, &lv_font_montserrat_48, 0);
  lv_obj_set_style_text_color(time_label, colWhite, 0);
  lv_obj_align(time_label, LV_ALIGN_TOP_MID, 0, 26);

  // 3. Ilustração do Clima Atual 3D
  weather_icon_box = lv_obj_create(left_panel);
  lv_obj_set_size(weather_icon_box, 80, 68);
  lv_obj_align(weather_icon_box, LV_ALIGN_TOP_MID, 0, 78);
  lv_obj_set_style_bg_opa(weather_icon_box, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(weather_icon_box, 0, 0);
  lv_obj_set_style_pad_all(weather_icon_box, 0, 0);
  lv_obj_clear_flag(weather_icon_box, LV_OBJ_FLAG_SCROLLABLE);
  render_weather_icon(weather_icon_box, currentWeatherCode);

  // 4. Temperatura Atual Grande (ex: "25 C")
  weather_temp_label = lv_label_create(left_panel);
  lv_label_set_text(weather_temp_label, weatherTemp.c_str());
  lv_obj_set_style_text_font(weather_temp_label, &lv_font_montserrat_48, 0);
  lv_obj_set_style_text_color(weather_temp_label, colWhite, 0);
  lv_obj_align(weather_temp_label, LV_ALIGN_TOP_MID, 0, 146);

  // 5. Nome da Cidade centralizado (ex: "NEPOMUCENO")
  weather_city_label = lv_label_create(left_panel);
  String cUpper = sanitize_for_lvgl(weatherCity);
  cUpper.toUpperCase();
  lv_label_set_text(weather_city_label, cUpper.c_str());
  lv_obj_set_style_text_font(weather_city_label, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(weather_city_label, colWhite, 0);
  lv_obj_set_style_text_letter_space(weather_city_label, 1, 0);
  lv_obj_align(weather_city_label, LV_ALIGN_TOP_MID, 0, 202);

  // 6. Subtítulo "PREVISÃO SEMANAL"
  lv_obj_t *fcTitle = lv_label_create(left_panel);
  lv_label_set_text(fcTitle, "PREVISAO SEMANAL");
  lv_obj_set_style_text_font(fcTitle, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(fcTitle, colMuted, 0);
  lv_obj_set_style_text_letter_space(fcTitle, 1, 0);
  lv_obj_align(fcTitle, LV_ALIGN_TOP_MID, 0, 236);

  // 7. Container da Previsão Semanal (6 Colunas)
  lv_obj_t *forecast_card = lv_obj_create(left_panel);
  lv_obj_set_pos(forecast_card, 6, 260);
  lv_obj_set_size(forecast_card, 338, 148);
  lv_obj_set_style_bg_color(forecast_card, colSubCard, 0);
  lv_obj_set_style_bg_opa(forecast_card, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(forecast_card, 14, 0);
  lv_obj_set_style_border_width(forecast_card, 1, 0);
  lv_obj_set_style_border_color(forecast_card, colBorder, 0);
  lv_obj_set_style_pad_all(forecast_card, 4, 0);
  lv_obj_clear_flag(forecast_card, LV_OBJ_FLAG_SCROLLABLE);

  int colX[6] = {4, 59, 114, 169, 224, 279};
  for (int i = 0; i < 6; i++) {
    // Dia da semana (QUI, SEX, SAB, DOM, SEG, SEG)
    lv_obj_t *dLbl = lv_label_create(forecast_card);
    lv_label_set_text(dLbl, weeklyForecast[i].dayName.c_str());
    lv_obj_set_style_text_font(dLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(dLbl, colWhite, 0);
    lv_obj_set_pos(dLbl, colX[i] + 8, 12);
    forecast_day_labels[i] = dLbl;

    // Mini Ícone
    lv_obj_t *iconCont = lv_obj_create(forecast_card);
    lv_obj_set_size(iconCont, 30, 28);
    lv_obj_set_pos(iconCont, colX[i] + 8, 44);
    lv_obj_set_style_bg_opa(iconCont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(iconCont, 0, 0);
    lv_obj_set_style_pad_all(iconCont, 0, 0);
    lv_obj_clear_flag(iconCont, LV_OBJ_FLAG_SCROLLABLE);
    render_mini_weather_icon(iconCont, weeklyForecast[i].weatherCode);
    forecast_icon_boxes[i] = iconCont;

    // Temperatura (MAX 26° / MIN 16°)
    char tBuf[16];
    if (weeklyForecast[i].isMin) {
      snprintf(tBuf, sizeof(tBuf), "MIN %d", weeklyForecast[i].tempVal);
    } else {
      snprintf(tBuf, sizeof(tBuf), "MAX %d", weeklyForecast[i].tempVal);
    }
    lv_obj_t *tLbl = lv_label_create(forecast_card);
    lv_label_set_text(tLbl, tBuf);
    lv_obj_set_style_text_font(tLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(tLbl, colMuted, 0);
    lv_obj_set_pos(tLbl, colX[i] + 2, 94);
    forecast_temp_labels[i] = tLbl;
  }

  // ==========================================
  // PAINEL DIREITO: 4 CARDS INDIVIDUAIS DE MOEDAS COM SPARKLINES
  // ==========================================
  lv_obj_t *moeda_title = lv_label_create(scr);
  lv_label_set_text(moeda_title, "COTACAO DE MOEDAS");
  lv_obj_set_style_text_font(moeda_title, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(moeda_title, lv_color_hex(0xC4CFDE), 0);
  lv_obj_set_style_text_letter_space(moeda_title, 2, 0);
  lv_obj_set_pos(moeda_title, 500, 16);

  const char* defaultPairs[4] = {"BTC-BRL", "USD-BRL", "ETH-BRL", "EUR-BRL"};
  const char* pairs[6] = {gConfig.currency_1, gConfig.currency_2, gConfig.currency_3, gConfig.currency_4, gConfig.currency_5, gConfig.currency_6};
  bool enabled[6] = {gConfig.curr1_enabled, gConfig.curr2_enabled, gConfig.curr3_enabled, gConfig.curr4_enabled, gConfig.curr5_enabled, gConfig.curr6_enabled};

  int activeIdx[4] = {-1, -1, -1, -1};
  int found = 0;
  for (int i = 0; i < 6 && found < 4; i++) {
    if (enabled[i]) {
      activeIdx[found++] = i;
    }
  }

  int cardY[4] = {46, 144, 242, 340};

  for (int row = 0; row < 4; row++) {
    int cfgIdx = (row < found) ? activeIdx[row] : row;
    const char* curPair = (row < found && strlen(pairs[cfgIdx]) > 0) ? pairs[cfgIdx] : defaultPairs[row];

    // Card Individual com Efeito Translúcido / Vidro
    lv_obj_t *card = lv_obj_create(scr);
    lv_obj_set_pos(card, 404, cardY[row]);
    lv_obj_set_size(card, 376, 88);
    lv_obj_set_style_bg_color(card, colCard, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, colBorder, 0);
    lv_obj_set_style_shadow_width(card, 10, 0);
    lv_obj_set_style_shadow_color(card, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(card, 40, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    moeda_cards[row] = card;

    // Ícone Circular da Moeda
    lv_obj_t *iconBox = lv_obj_create(card);
    lv_obj_set_size(iconBox, 44, 44);
    lv_obj_set_pos(iconBox, 14, 22);
    lv_obj_set_style_bg_opa(iconBox, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(iconBox, 0, 0);
    lv_obj_set_style_pad_all(iconBox, 0, 0);
    lv_obj_clear_flag(iconBox, LV_OBJ_FLAG_SCROLLABLE);
    render_currency_icon(iconBox, curPair);
    moeda_icon_boxes[row] = iconBox;

    // Par da Moeda (ex: "BTC/BRL")
    String pStr = String(curPair);
    pStr.replace("-", "/");
    lv_obj_t *pairLbl = lv_label_create(card);
    lv_label_set_text(pairLbl, pStr.c_str());
    lv_obj_set_style_text_font(pairLbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(pairLbl, colWhite, 0);
    lv_obj_set_pos(pairLbl, 68, 18);
    moeda_pair_labels[row] = pairLbl;

    // Subtítulo da Moeda (ex: "Bitcoin")
    lv_obj_t *subLbl = lv_label_create(card);
    lv_label_set_text(subLbl, get_currency_friendly_name(curPair));
    lv_obj_set_style_text_font(subLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(subLbl, colMuted, 0);
    lv_obj_set_pos(subLbl, 68, 48);
    moeda_sub_labels[row] = subLbl;

    // Valor da Moeda (ex: "R$ 404.208")
    lv_obj_t *valLbl = lv_label_create(card);
    lv_label_set_text(valLbl, moedaValues[cfgIdx].c_str());
    lv_obj_set_style_text_font(valLbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(valLbl, colWhite, 0);
    lv_obj_align(valLbl, LV_ALIGN_TOP_RIGHT, -16, 18);
    moeda_value_labels[row] = valLbl;

    // Mini Gráfico Sparkline de Tendência
    lv_obj_t *line = lv_line_create(card);
    lv_obj_set_pos(line, 204, 52);
    lv_obj_set_style_line_width(line, 2, 0);
    lv_obj_set_style_line_rounded(line, true, 0);
    moeda_lines[row] = line;
    update_sparkline(row, moedaPctPos[cfgIdx]);

    // Variação Percentual (ex: "-1,01%" ou "+0,26%")
    lv_obj_t *pctLbl = lv_label_create(card);
    lv_label_set_text(pctLbl, moedaPcts[cfgIdx].c_str());
    lv_obj_set_style_text_font(pctLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(pctLbl, moedaPctPos[cfgIdx] ? lv_color_hex(0x22C55E) : lv_color_hex(0xEF4444), 0);
    lv_obj_align(pctLbl, LV_ALIGN_TOP_RIGHT, -16, 50);
    moeda_pct_labels[row] = pctLbl;
  }

  // ==========================================
  // RODAPÉ: BARRAS VERDES DE WIFI + VERSÃO DISCRETA
  // ==========================================
  for (int b = 0; b < 4; b++) {
    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_set_size(bar, 3, 4 + b * 3);
    lv_obj_set_pos(bar, 22 + b * 5, 462 - (b * 3));
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x22C55E), 0);
    lv_obj_set_style_radius(bar, 1, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    wifi_bars[b] = bar;
  }

  bottom_wifi_label = lv_label_create(scr);
  if (WiFi.status() == WL_CONNECTED) {
    char buf[64];
    snprintf(buf, sizeof(buf), "WIFI: %s", WiFi.localIP().toString().c_str());
    lv_label_set_text(bottom_wifi_label, buf);
  } else {
    lv_label_set_text(bottom_wifi_label, "WIFI: 192.168.4.1 (AP)");
  }
  lv_obj_set_style_text_font(bottom_wifi_label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(bottom_wifi_label, colMuted, 0);
  lv_obj_set_pos(bottom_wifi_label, 48, 452);

  bottom_version_label = lv_label_create(scr);
  lv_label_set_text(bottom_version_label, "v2.1.5");
  lv_obj_set_style_text_font(bottom_version_label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(bottom_version_label, lv_color_hex(0x64748B), 0);
  lv_obj_align(bottom_version_label, LV_ALIGN_BOTTOM_RIGHT, -24, -12);
}

// Atualização do Relógio e Data a cada segundo
void update_clock(lv_timer_t *timer) {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    char timeStr[16];
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);

    static const char *weekdays[] = {"DOM", "SEG", "TER", "QUA", "QUI", "SEX", "SAB"};
    static const char *months[] = {"JAN", "FEV", "MAR", "ABR", "MAI", "JUN", "JUL", "AGO", "SET", "OUT", "NOV", "DEZ"};
    char dateStr[32];
    snprintf(dateStr, sizeof(dateStr), "%s, %02d %s",
             weekdays[timeinfo.tm_wday], timeinfo.tm_mday, months[timeinfo.tm_mon]);

    if (time_label) lv_label_set_text(time_label, timeStr);
    if (date_label) lv_label_set_text(date_label, dateStr);
  }

  if (bottom_wifi_label) {
    if (WiFi.status() == WL_CONNECTED) {
      char buf[64];
      snprintf(buf, sizeof(buf), "WIFI: %s", WiFi.localIP().toString().c_str());
      lv_label_set_text(bottom_wifi_label, buf);
    } else {
      lv_label_set_text(bottom_wifi_label, "WIFI: 192.168.4.1 (AP)");
    }
  }
}

// Formatação brasileira de moedas com separador de milhar
String format_currency_value(float val) {
  char buf[32];
  if (val >= 1000.0f) {
    long intVal = (long)val;
    if (intVal >= 1000000) {
      snprintf(buf, sizeof(buf), "R$ %ld.%03ld.%03ld", intVal / 1000000, (intVal % 1000000) / 1000, intVal % 1000);
    } else {
      snprintf(buf, sizeof(buf), "R$ %ld.%03ld", intVal / 1000, intVal % 1000);
    }
  } else {
    snprintf(buf, sizeof(buf), "R$ %.2f", val);
    char *dot = strchr(buf, '.');
    if (dot) *dot = ',';
  }
  return String(buf);
}

// Atualização de Cotação de Moedas em Tempo Real via AwesomeAPI
void update_dolar(lv_timer_t *timer) {
  if (WiFi.status() != WL_CONNECTED) return;
  String pairs[6] = {String(gConfig.currency_1), String(gConfig.currency_2), String(gConfig.currency_3), String(gConfig.currency_4), String(gConfig.currency_5), String(gConfig.currency_6)};
  bool enabled[6] = {gConfig.curr1_enabled, gConfig.curr2_enabled, gConfig.curr3_enabled, gConfig.curr4_enabled, gConfig.curr5_enabled, gConfig.curr6_enabled};
  String list = "";
  for (int i = 0; i < 6; i++) {
    if (enabled[i] && pairs[i].length() > 0) {
      if (list.length()) list += ",";
      list += pairs[i];
    }
  }
  if (list.length() == 0) list = "BTC-BRL,USD-BRL,ETH-BRL,EUR-BRL";

  String url = "https://economia.awesomeapi.com.br/json/last/" + list;
  HTTPClient http;
  http.setTimeout(10000);
  http.begin(url);
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (!err) {
      int idx = 0;
      for (int i = 0; i < 6 && idx < 4; i++) {
        if (enabled[i] && pairs[i].length() > 0) {
          String key = pairs[i];
          key.replace("-", "");
          if (doc[key].is<JsonObject>()) {
            float bid = doc[key]["bid"].as<float>();
            float pct = doc[key]["pctChange"].as<float>();

            String valStr = format_currency_value(bid);
            moedaValues[i] = valStr;
            if (i == 0) dolarValue = valStr;

            char pctBuf[24];
            bool isPos = (pct >= 0);
            if (isPos) {
              snprintf(pctBuf, sizeof(pctBuf), "^ +%.2f%%", pct);
            } else {
              snprintf(pctBuf, sizeof(pctBuf), "v %.2f%%", pct);
            }
            char *pdot = strchr(pctBuf, '.');
            if (pdot) *pdot = ',';

            moedaPcts[i] = String(pctBuf);
            moedaPctPos[i] = isPos;

            if (moeda_value_labels[idx]) {
              lv_label_set_text(moeda_value_labels[idx], valStr.c_str());
            }
            if (moeda_pct_labels[idx]) {
              lv_label_set_text(moeda_pct_labels[idx], pctBuf);
              lv_obj_set_style_text_color(moeda_pct_labels[idx], isPos ? lv_color_hex(0x22C55E) : lv_color_hex(0xEF4444), 0);
            }
            if (moeda_pair_labels[idx]) {
              String p = pairs[i];
              p.replace("-", "/");
              lv_label_set_text(moeda_pair_labels[idx], p.c_str());
            }
            if (moeda_sub_labels[idx]) {
              lv_label_set_text(moeda_sub_labels[idx], get_currency_friendly_name(pairs[i].c_str()));
            }
            if (moeda_icon_boxes[idx]) {
              render_currency_icon(moeda_icon_boxes[idx], pairs[i].c_str());
            }
            update_sparkline(idx, isPos);

            idx++;
          }
        }
      }
    }
  }
  http.end();
}

// Atualização do Clima Atual e Previsão Semanal em Tempo Real via Open-Meteo
void update_weather(lv_timer_t *timer) {
  if (WiFi.status() != WL_CONNECTED) return;

  char url[256];
  snprintf(url, sizeof(url), "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f&current=temperature_2m,weather_code&daily=weather_code,temperature_2m_max&timezone=auto&forecast_days=6", gConfig.lat, gConfig.lon);
  HTTPClient http;
  http.setTimeout(10000);
  http.begin(url);
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (!err) {
      int wcode = 61;
      float curTemp = 25.0f;

      if (doc["current"].is<JsonObject>()) {
        curTemp = doc["current"]["temperature_2m"].as<float>();
        wcode = doc["current"]["weather_code"].as<int>();
      }

      currentWeatherCode = wcode;
      weatherCity = String(gConfig.city);

      char bufTemp[16];
      snprintf(bufTemp, sizeof(bufTemp), "%d C", (int)round(curTemp));
      weatherTemp = bufTemp;

      if (weather_temp_label) lv_label_set_text(weather_temp_label, weatherTemp.c_str());
      if (weather_city_label) {
        String cUpper = sanitize_for_lvgl(weatherCity);
        cUpper.toUpperCase();
        lv_label_set_text(weather_city_label, cUpper.c_str());
      }
      if (weather_icon_box) {
        render_weather_icon(weather_icon_box, currentWeatherCode);
      }

      // Processa a Previsão Semanal para os 6 Dias
      if (doc["daily"].is<JsonObject>()) {
        static const char *weekdays[] = {"DOM", "SEG", "TER", "QUA", "QUI", "SEX", "SAB"};
        struct tm timeinfo;
        int curWday = 3; // Default QUA
        if (getLocalTime(&timeinfo)) curWday = timeinfo.tm_wday;

        for (int i = 0; i < 6; i++) {
          int dWday = (curWday + 1 + i) % 7;
          weeklyForecast[i].dayName = weekdays[dWday];
          if (doc["daily"]["temperature_2m_max"][i].is<float>()) {
            weeklyForecast[i].tempVal = (int)round(doc["daily"]["temperature_2m_max"][i].as<float>());
          }
          if (doc["daily"]["weather_code"][i].is<int>()) {
            weeklyForecast[i].weatherCode = doc["daily"]["weather_code"][i].as<int>();
          }

          if (forecast_day_labels[i]) lv_label_set_text(forecast_day_labels[i], weeklyForecast[i].dayName.c_str());
          if (forecast_temp_labels[i]) {
            char tBuf[16];
            if (weeklyForecast[i].isMin) {
              snprintf(tBuf, sizeof(tBuf), "MIN %d", weeklyForecast[i].tempVal);
            } else {
              snprintf(tBuf, sizeof(tBuf), "MAX %d", weeklyForecast[i].tempVal);
            }
            lv_label_set_text(forecast_temp_labels[i], tBuf);
          }
          if (forecast_icon_boxes[i]) {
            render_mini_weather_icon(forecast_icon_boxes[i], weeklyForecast[i].weatherCode);
          }
        }
      }
    }
  }
  http.end();
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== SMART DASHBOARD BOOT ===");
  loadConfig();

  lv_init();
  tft.init();
  tft.setRotation(0);
  tft.setBrightness(gConfig.brightness);
  tft.fillScreen(TFT_BLACK);

  // Buffer no IRAM interno rápido para estabilidade total contra flicker/concorrência PSRAM
  buf1 = (lv_color_t *)heap_caps_malloc(800 * BUF_LINES * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!buf1) buf1 = (lv_color_t *)heap_caps_malloc(800 * BUF_LINES * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  buf2 = nullptr;
  lv_disp_draw_buf_init(&draw_buf, buf1, buf2, 800 * BUF_LINES);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = 800;
  disp_drv.ver_res = 480;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_touch_read;
  lv_indev_drv_register(&indev_drv);

  create_ui();

  // AP de configuracao SEMPRE ligado para garantir acesso caso WiFi falhe
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  WiFi.softAP("Painel-Config", "12345678");

  webServerInit();

  if (gConfig.wifi_ssid[0] != '\0') {
    Serial.printf("[WiFi] Conectando em '%s' ...\n", gConfig.wifi_ssid);
    WiFi.begin(gConfig.wifi_ssid, gConfig.wifi_pass);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
      lv_timer_handler();
      webServerLoop();
      delay(10);
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (bottom_wifi_label) {
      char buf[64];
      snprintf(buf, sizeof(buf), "WIFI: %s", WiFi.localIP().toString().c_str());
      lv_label_set_text(bottom_wifi_label, buf);
    }
  }

  configTime(gConfig.tz_offset * 3600, 0, "pool.ntp.org", "time.nist.gov");

  struct tm timeinfo;
  int ntpWait = 0;
  while (!getLocalTime(&timeinfo) && ntpWait < 10) {
    lv_timer_handler();
    webServerLoop();
    delay(500);
    ntpWait++;
  }

  lv_timer_create(update_clock, 1000, NULL);
  lv_timer_create(update_dolar, gConfig.dolar_interval * 1000, NULL);
  lv_timer_create(update_weather, gConfig.weather_interval * 1000, NULL);
  update_dolar(NULL);
  update_weather(NULL);
  update_clock(NULL);

  otaInit();
}

void loop() {
  if (gNeedsRebuild) {
    gNeedsRebuild = false;
    create_ui();
    update_dolar(NULL);
    update_weather(NULL);
    update_clock(NULL);
  }
  lv_timer_handler();
  webServerLoop();
  otaLoop();
  delay(5);
}
