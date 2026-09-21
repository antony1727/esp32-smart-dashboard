#pragma once
#include <Arduino.h>
#include <Preferences.h>

struct AppConfig {
  char wifi_ssid[64] = "ROBOBUILDERS";
  char wifi_pass[64] = "luan123*";
  // Moedas: awesomeapi pairs. Até 6 simultâneas
  char currency_1[16] = "BTC-BRL";
  char currency_2[16] = "USD-BRL";
  char currency_3[16] = "ETH-BRL";
  char currency_4[16] = "EUR-BRL";
  char currency_5[16] = "GBP-BRL";
  char currency_6[16] = "JPY-BRL";
  bool curr1_enabled = true;
  bool curr2_enabled = true;
  bool curr3_enabled = true;
  bool curr4_enabled = true;
  bool curr5_enabled = false;
  bool curr6_enabled = false;
  char city[64] = "Nepomuceno";
  float lat = -21.2333f;
  float lon = -45.2333f;
  uint8_t brightness = 180;
  int8_t tz_offset = -3; // -3 Brasil
  uint16_t dolar_interval = 60;   // segundos
  uint16_t weather_interval = 600; // segundos
  bool display_light = false; // false=noturno escuro, true=claro transfere p/ ESP
};

extern AppConfig gConfig;

void loadConfig();
void saveConfig();
void resetConfig();
