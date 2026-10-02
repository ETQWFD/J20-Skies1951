// settings.h - global display/language settings (persisted to skies_settings.txt)
#ifndef SETTINGS_H
#define SETTINGS_H

#define SKIES_VERSION "2.1.5"
#define SKIES_VERCODE 19

extern int gSetFPS;    // 0=60, 1=120, 2=uncapped(up to 480/hardware limit)
extern int gSetQuality;// 0 smooth, 1 classic, 2 HD, 3 real
extern int gSetLang;   // 0 zh, 1 en, 2 ja, 3 ru

void Settings_Load(void);
void Settings_Save(void);
void Settings_Apply(void);                 // push fps + grass density into the engine
float Settings_GrassDensity(void);

// run the settings screen; returns when the user chooses "back"
void Settings_Screen(void);

// tiny UI phrase table for the 4 supported languages (history/campaign proper
// names stay in their original Chinese as historical nouns)
const char* tr(const char* key);

#endif
