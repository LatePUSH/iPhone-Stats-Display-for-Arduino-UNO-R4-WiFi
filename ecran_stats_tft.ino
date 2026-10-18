

#include <ArduinoBLE.h>
#include <Arduino_GFX_Library.h>
#include "RTC.h"


#define ECRAN_ILI9488 0     
#define ROTATION      1    
#define COULEURS_IPS  false  
const unsigned long OBJECTIF_PAS = 10000;


#define TFT_CS  10
#define TFT_DC  9
#define TFT_RST 8

Arduino_DataBus *bus = new Arduino_HWSPI(TFT_DC, TFT_CS);
#if ECRAN_ILI9488
Arduino_GFX *gfx = new Arduino_ILI9488_18bit(bus, TFT_RST, ROTATION, COULEURS_IPS);
#else
Arduino_GFX *gfx = new Arduino_ST7796(bus, TFT_RST, ROTATION, COULEURS_IPS);
#endif


const uint16_t NOIR       = 0x0000;
const uint16_t BLANC      = 0xFFFF;
const uint16_t GRIS       = 0x8410;
const uint16_t GRIS_FONCE = 0x2945;
const uint16_t VERT       = 0x07E0;
const uint16_t ORANGE     = 0xFD20;
const uint16_t ROUGE      = 0xF800;
const uint16_t CYAN       = 0x07FF;
const uint16_t JAUNE      = 0xFFE0;


BLEService service("7A1E0000-4B5C-4F6D-9E8A-1B2C3D4E5F60");
BLEUnsignedLongCharacteristic heureChar("7A1E0001-4B5C-4F6D-9E8A-1B2C3D4E5F60", BLEWrite);
BLECharacteristic             batterieChar("7A1E0002-4B5C-4F6D-9E8A-1B2C3D4E5F60", BLEWrite, 2);
BLEUnsignedLongCharacteristic pasChar("7A1E0003-4B5C-4F6D-9E8A-1B2C3D4E5F60", BLEWrite);
BLEStringCharacteristic       evtHeureChar("7A1E0004-4B5C-4F6D-9E8A-1B2C3D4E5F60", BLEWrite, 20);
BLEStringCharacteristic       evtTitreChar("7A1E0005-4B5C-4F6D-9E8A-1B2C3D4E5F60", BLEWrite, 20);

const unsigned long INCONNU = 0xFFFFFFFF;

int batterie = -1;
bool enCharge = false;
unsigned long pas = INCONNU;
String evtHeure = "";
String evtTitre = "";
bool heureRecue = false;
bool donneesRecues = false;
bool etaitConnecte = false;
int derniereMinute = -1;


String sansAccents(const String& s) {
  static const char table[] =
    "AAAAAAAC" "EEEEIIII" "DNOOOOOx" "OUUUUYPs"
    "aaaaaaac" "eeeeiiii" "dnooooo/" "ouuuuypy";
  String r;
  r.reserve(s.length());
  for (unsigned int i = 0; i < s.length(); i++) {
    uint8_t c = s[i];
    if (c < 0x80) {
      r += (char)c;
    } else if (c == 0xC3 && i + 1 < s.length()) {
      uint8_t d = s[++i];
      if (d >= 0x80 && d <= 0xBF) r += table[d - 0x80];
    } else if (c == 0xE2 && i + 2 < s.length() && (uint8_t)s[i + 1] == 0x80) {
      uint8_t d = s[i + 2];
      i += 2;
      if (d == 0x98 || d == 0x99) r += '\'';      
      else if (d == 0x9C || d == 0x9D) r += '"';  
      else if (d == 0xA6) r += "...";             
    } else if (c >= 0xC0) {
      while (i + 1 < s.length() && ((uint8_t)s[i + 1] & 0xC0) == 0x80) i++;
      r += '?';
    }
  }
  return r;
}

void texte(int x, int y, const String& s, uint8_t taille, uint16_t couleur) {
  gfx->setTextSize(taille);   
  gfx->setTextColor(couleur);
  gfx->setCursor(x, y);
  gfx->print(s);
}

String formaterPas(unsigned long n) {
  char txt[16];
  if (n >= 1000) snprintf(txt, sizeof(txt), "%lu %03lu", n / 1000, n % 1000);
  else           snprintf(txt, sizeof(txt), "%lu", n);
  return String(txt);
}



void dessinerHorloge() {
  gfx->fillRect(0, 0, 300, 112, NOIR);
  if (heureRecue) {
    RTCTime t;
    RTC.getTime(t);
    derniereMinute = t.getMinutes();
    char h[6];
    snprintf(h, sizeof(h), "%02d:%02d", t.getHour(), t.getMinutes());
    texte(20, 28, h, 8, BLANC);
  } else {
    texte(20, 28, "--:--", 8, GRIS);
  }
}

void dessinerBLE() {
  gfx->fillCircle(462, 14, 7, BLE.connected() ? VERT : GRIS_FONCE);
}

void dessinerBatterie() {
  gfx->fillRect(300, 24, 180, 90, NOIR);

  // Contour de la pile
  gfx->drawRect(330, 30, 110, 50, BLANC);
  gfx->drawRect(331, 31, 108, 48, BLANC);
  gfx->fillRect(440, 45, 8, 20, BLANC);

  if (batterie < 0) {
    texte(376, 44, "?", 3, GRIS);
    return;
  }


  uint16_t couleur = batterie > 50 ? VERT : (batterie > 20 ? ORANGE : ROUGE);
  gfx->fillRect(335, 35, batterie, 40, couleur);

  String txt = String(batterie) + "%";
  if (enCharge) txt += " +";
  texte(330, 88, txt, 3, couleur);
}

void dessinerPas() {
  gfx->fillRect(0, 124, 480, 88, NOIR);
  texte(20, 130, "PAS AUJOURD'HUI", 2, GRIS);

  if (pas == INCONNU) {
    texte(20, 155, "--", 4, BLANC);
    return;
  }

  texte(20, 155, formaterPas(pas), 4, BLANC);

 
  unsigned long pourcent = (pas * 100) / OBJECTIF_PAS;
  String txtPourcent = String(pourcent) + "%";
  texte(460 - txtPourcent.length() * 18, 160, txtPourcent, 3, GRIS);


  unsigned long plafonne = pas > OBJECTIF_PAS ? OBJECTIF_PAS : pas;
  int largeur = (436UL * plafonne) / OBJECTIF_PAS;
  gfx->drawRect(20, 195, 440, 12, GRIS_FONCE);
  gfx->fillRect(22, 197, largeur, 8, pas >= OBJECTIF_PAS ? VERT : CYAN);
}

void dessinerEvenement() {
  gfx->fillRect(0, 222, 480, 98, NOIR);
  texte(20, 228, "PROCHAIN RENDEZ-VOUS", 2, GRIS);
  texte(20, 254, sansAccents(evtHeure), 3, JAUNE);
  texte(20, 288, sansAccents(evtTitre), 3, BLANC);
}

void dessinerTout() {
  gfx->fillScreen(NOIR);

  if (!donneesRecues) {
    texte(20, 100, "En attente", 4, BLANC);
    texte(20, 150, "de l'iPhone...", 4, BLANC);
    texte(20, 230, BLE.connected() ? "Connecte" : "Bluetooth pret", 2, GRIS);
    return;
  }

  dessinerHorloge();
  dessinerBLE();
  dessinerBatterie();
  gfx->drawFastHLine(20, 118, 440, GRIS_FONCE);
  dessinerPas();
  gfx->drawFastHLine(20, 216, 440, GRIS_FONCE);
  dessinerEvenement();
}



void setup() {
  Serial.begin(115200);
  RTC.begin();

  if (!gfx->begin()) {
    Serial.println("Erreur : ecran non detecte");
  }

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
  dessinerTout();
}

void loop() {
  BLE.poll();

  bool avaitDesDonnees = donneesRecues;
  bool chHeure = false, chBatterie = false, chPas = false, chEvenement = false;

  if (heureChar.written()) {
    RTCTime t((time_t)heureChar.value());
    RTC.setTime(t);
    heureRecue = true;
    chHeure = true;
  }
  if (batterieChar.written() && batterieChar.valueLength() >= 2) {
    const uint8_t* v = batterieChar.value();
    batterie = (v[0] == 255) ? -1 : v[0];
    enCharge = v[1] == 1;
    chBatterie = true;
  }
  if (pasChar.written()) {
    pas = pasChar.value();
    chPas = true;
  }
  if (evtHeureChar.written()) {
    evtHeure = evtHeureChar.value();
    chEvenement = true;
  }
  if (evtTitreChar.written()) {
    evtTitre = evtTitreChar.value();
    chEvenement = true;
  }

  if (chHeure || chBatterie || chPas || chEvenement) donneesRecues = true;


  if (donneesRecues && !avaitDesDonnees) {
    dessinerTout();
  } else if (donneesRecues) {
    if (chHeure)     dessinerHorloge();
    if (chBatterie)  dessinerBatterie();
    if (chPas)       dessinerPas();
    if (chEvenement) dessinerEvenement();
  }


  if (donneesRecues && heureRecue) {
    RTCTime t;
    RTC.getTime(t);
    if (t.getMinutes() != derniereMinute) dessinerHorloge();
  }


  bool connecte = BLE.connected();
  if (connecte != etaitConnecte) {
    etaitConnecte = connecte;
    if (donneesRecues) dessinerBLE();
    else               dessinerTout();
  }
}
