/*
  Écran de stats - Arduino UNO R4 WiFi + écran OLED I2C + app iOS
  L'iPhone envoie en BLE : l'heure, sa batterie, les pas du jour, le prochain événement.
  L'Arduino les affiche sur l'OLED 128x64.

*/

#include <ArduinoBLE.h>
#include <Wire.h>
#include <U8g2lib.h>
#include "RTC.h"

// Temps écran

// 0 = 0,96" (SSD1306)   1 = 1,3" (SH1106)
#define ECRAN_SH1106 0


#if ECRAN_SH1106
U8G2_SH1106_128X64_NONAME_F_HW_I2C ecran(U8G2_R0, U8X8_PIN_NONE);
#else
U8G2_SSD1306_128X64_NONAME_F_HW_I2C ecran(U8G2_R0, U8X8_PIN_NONE);
#endif

// UUID partagés avec l'app iOS
BLEService service("7A1E0000-4B5C-4F6D-9E8A-1B2C3D4E5F60");
BLEUnsignedLongCharacteristic heureChar("7A1E0001-4B5C-4F6D-9E8A-1B2C3D4E5F60", BLEWrite);     // heure locale (secondes)
BLECharacteristic             batterieChar("7A1E0002-4B5C-4F6D-9E8A-1B2C3D4E5F60", BLEWrite, 2); // [niveau, en charge]
BLEUnsignedLongCharacteristic pasChar("7A1E0003-4B5C-4F6D-9E8A-1B2C3D4E5F60", BLEWrite);       // pas du jour
BLEStringCharacteristic       evtHeureChar("7A1E0004-4B5C-4F6D-9E8A-1B2C3D4E5F60", BLEWrite, 20);
BLEStringCharacteristic       evtTitreChar("7A1E0005-4B5C-4F6D-9E8A-1B2C3D4E5F60", BLEWrite, 20);

const unsigned long INCONNU = 0xFFFFFFFF;

int batterie = -1;            // -1 = inconnue
bool enCharge = false;
unsigned long pas = INCONNU;
String evtHeure = "";
String evtTitre = "";
bool heureRecue = false;
bool donneesRecues = false;

unsigned long dernierDessin = 0;

// ---------- Dessin ----------
void formaterPas(unsigned long n, char* sortie, size_t taille) {
  if (n == INCONNU)   snprintf(sortie, taille, "-- pas");
  else if (n >= 1000) snprintf(sortie, taille, "%lu %03lu pas", n / 1000, n % 1000);
  else                snprintf(sortie, taille, "%lu pas", n);
}

void dessinerBatterie() {
  // Icône de pile en haut à droite
  ecran.drawFrame(106, 3, 18, 9);
  ecran.drawBox(124, 5, 2, 5);
  if (batterie >= 0) {
    ecran.drawBox(108, 5, (14 * batterie) / 100, 5);
  }

  // Pourcentage à gauche de l'icône
  char txt[8];
  if (batterie < 0) snprintf(txt, sizeof(txt), "?");
  else              snprintf(txt, sizeof(txt), "%s%d%%", enCharge ? "+" : "", batterie);
  ecran.setFont(u8g2_font_6x10_tf);
  ecran.drawStr(103 - ecran.getStrWidth(txt), 11, txt);
}

void dessiner() {
  ecran.clearBuffer();

  if (!donneesRecues) {
    ecran.setFont(u8g2_font_6x12_tf);
    ecran.drawUTF8(0, 22, "En attente");
    ecran.drawUTF8(0, 36, "de l'iPhone...");
    ecran.drawUTF8(0, 56, BLE.connected() ? "Connecté" : "Bluetooth prêt");
    ecran.sendBuffer();
    return;
  }

  // Ligne 1 : heure + indicateur Bluetooth + batterie
  ecran.setFont(u8g2_font_7x14B_tf);
  if (heureRecue) {
    RTCTime t;
    RTC.getTime(t);
    char h[6];
    snprintf(h, sizeof(h), "%02d:%02d", t.getHour(), t.getMinutes());
    ecran.drawStr(0, 12, h);
  } else {
    ecran.drawStr(0, 12, "--:--");
  }
  if (BLE.connected()) ecran.drawDisc(44, 7, 2);    // point plein = connecté
  else                 ecran.drawCircle(44, 7, 2);  // cercle vide = déconnecté
  dessinerBatterie();

  ecran.drawHLine(0, 16, 128);

  // Ligne 2 : pas du jour
  char txtPas[20];
  formaterPas(pas, txtPas, sizeof(txtPas));
  ecran.setFont(u8g2_font_helvB10_tf);
  ecran.drawStr(0, 31, txtPas);

  ecran.drawHLine(0, 36, 128);

  // Lignes 3 et 4 : prochain événement
  ecran.setFont(u8g2_font_6x12_tf);
  ecran.drawUTF8(0, 49, evtHeure.c_str());
  ecran.drawUTF8(0, 62, evtTitre.c_str());

  ecran.sendBuffer();
}

// ---------- Programme principal ----------
void setup() {
  Serial.begin(115200);
  ecran.begin();
  RTC.begin();

  if (!BLE.begin()) {
    Serial.println("Erreur : BLE non disponible");
    while (true);
  }
  BLE.setLocalName("Stats R4");
  BLE.setAdvertisedService(service);
  service.addCharacteristic(heureChar);
  service.addCharacteristic(batterieChar);
  service.addCharacteristic(pasChar);
  service.addCharacteristic(evtHeureChar);
  service.addCharacteristic(evtTitreChar);
  BLE.addService(service);
  BLE.advertise();

  Serial.println("Pret, en attente de l'iPhone");
  dessiner();
}

void loop() {
  BLE.poll();
  bool change = false;

  if (heureChar.written()) {
    RTCTime t((time_t)heureChar.value());
    RTC.setTime(t);
    heureRecue = true;
    change = true;
  }
  if (batterieChar.written() && batterieChar.valueLength() >= 2) {
    const uint8_t* v = batterieChar.value();
    batterie = (v[0] == 255) ? -1 : v[0];
    enCharge = v[1] == 1;
    change = true;
  }
  if (pasChar.written()) {
    pas = pasChar.value();
    change = true;
  }
  if (evtHeureChar.written()) {
    evtHeure = evtHeureChar.value();
    change = true;
  }
  if (evtTitreChar.written()) {
    evtTitre = evtTitreChar.value();
    change = true;
  }
  if (change) donneesRecues = true;

  // Redessine dès qu'une donnée change, et chaque seconde pour l'horloge
  if (change || millis() - dernierDessin > 1000) {
    dessiner();
    dernierDessin = millis();
  }
}
