// ui_native.h - cross-platform bridge to the platform's real text input
// (Android soft keyboard / IME), clipboard and a writable config directory.
// Desktop builds use raylib's own keyboard/clipboard and the current directory.
#ifndef UI_NATIVE_H
#define UI_NATIVE_H

// Writable, permission-free absolute path for a config/save file.
// On Android this is the app's internal files dir (no runtime permission);
// on desktop it is just the bare filename (current working directory).
void  Skies_ConfigPath(char* out, int cap, const char* name);

// Show / hide the system soft keyboard (Android). No-op on desktop.
// `current` seeds the field with the text already typed.
void  Skies_IME_Open(const char* current);
void  Skies_IME_Close(void);
// Replace the platform field's text (used after a Paste so the IME field and
// the in-game buffer stay in sync).
void  Skies_IME_SetText(const char* text);
// Pending input from the soft keyboard: one unicode codepoint, or 0.
int   Skies_IME_Char(void);
// Edge events from the soft keyboard (consume once).
int   Skies_IME_BackspacePressed(void);
int   Skies_IME_EnterPressed(void);

// System clipboard. Never NULL; returns "" when empty.
const char* Skies_ClipGet(void);
void        Skies_ClipSet(const char* text);

#endif
