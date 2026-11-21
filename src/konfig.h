/*
 * konfig.h - Hardware-Konstanten, Pins und Grenzwerte
 *
 * Alles, was vom Aufbau abhängt (Verdrahtung, Referenz, Wägezelle), steht hier.
 */
#pragma once

#include <Arduino.h>

// ===========================================================================
// ADS1256 an SPI0
// ===========================================================================
constexpr uint8_t PIN_SPI_SCK  = 18;
constexpr uint8_t PIN_SPI_MOSI = 19;   // Pico TX -> ADS1256 DIN
constexpr uint8_t PIN_SPI_MISO = 16;   // Pico RX <- ADS1256 DOUT
constexpr uint8_t PIN_ADS_CS   = 17;   // manuell als GPIO
constexpr uint8_t PIN_ADS_DRDY = 20;
constexpr int8_t  PIN_ADS_PDWN = 21;   // -1, falls nicht verwendet

// ===========================================================================
// Schrittmotortreiber HB808C (Signaleingänge 4,5-28 V -> 5-V-Pegelwandler bzw. Transistoren nötig!)
// ===========================================================================
constexpr uint8_t PIN_MOT_PUL     = 2;   // Schrittimpulse (PWM)
constexpr uint8_t PIN_MOT_DIR     = 3;   // Richtung
constexpr uint8_t PIN_MOT_ENA     = 4;   // Freigabe (optional, siehe 'mp ena')
constexpr uint8_t PIN_MOT_ALM     = 5;   // Alarmausgang des Treibers (optional, siehe 'mp alm')
constexpr uint8_t PIN_TASTE_START = 6;   // Taster gegen GND: Endpunkt-Kalibrierung starten
constexpr uint8_t PIN_TASTE_STOPP = 7;   // Taster gegen GND: sofort anhalten

// Nur für den Prüfbefehl 'enc': Encoder EA+/EB+ des Motors über Spannungsteiler 10k/20k (5 V -> 3,3 V)
constexpr uint8_t PIN_GEBER_A     = 10;
constexpr uint8_t PIN_GEBER_B     = 11;

// ===========================================================================
// Analoge Kenngrößen und Grenzwerte
// ===========================================================================
constexpr float    VREF_V         = 2.50f;      // ADR03
constexpr uint32_t SPI_TAKT_HZ    = 1900000;    // max. fCLKIN/4 = 1,92 MHz
constexpr float    ERREGUNG_V     = 5.00f;      // Brückenspeisung (LT3042)
constexpr float    NENNLAST_G     = 50000.0f;   // 50 kg
constexpr float    WARNGRENZE_G   = 50000.0f;   // 100 % Nennlast
constexpr float    ALARMGRENZE_G  = 60000.0f;   // 120 % (sichere Überlast)
constexpr float    WARN_HYSTERESE = 0.98f;      // Rückfall erst unter 98 % der Grenze

// Pressweg: gepresst (und aufgezeichnet) wird ab höchstens so weit über dem kalibrierten Werkzeug-Endpunkt
// ('mp anfahrt', bis zum größten Hub von 45 mm), darüber Anfahrt. Die 960 Punkte der Pico-Kurve verteilen sich
// auf diesen Weg, daher so knapp wie möglich einstellen. Standard ist PRESSWEG_STANDARD_MM.
constexpr float    PRESSWEG_MAX_MM      = 45.0f;
constexpr float    PRESSWEG_STANDARD_MM = 5.0f;
// Pressvorgang: Geschwindigkeit der Anfahrt bis zum Anfahrpunkt und der Rückfahrt ('mp vanfahrt' / 'mp vrueck')
constexpr float    PRESSE_V_STANDARD_MMS = 3.0f;
constexpr float    PRESSE_V_MIN_MMS      = 0.1f;
constexpr float    PRESSE_V_MAX_MMS      = 50.0f;
// Hub kalibrieren: nach dem Endpunkt so weit über den Werkzeug-Endpunkt zurückfahren ('mp kalrueck', 0 = zurück
// zur Startposition). Höchstens der nutzbare Hub ('mp hub'), auch wenn das über der Startposition liegt.
constexpr float    KAL_RUECK_MAX_MM      = 45.0f;

// Startwert für den Kalibrierfaktor, solange nicht kalibriert wurde:
// 1,5 mV/V * 5 V = 7,5 mV bei 50 kg; 1 Count = 9,313 nV  ->  ca. 16,1 Counts/g
constexpr float STANDARD_FAKTOR = 16.1f;
