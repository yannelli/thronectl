#pragma once
#include "../core/ui.h"

Screen* makeSplash();
Screen* makeHome();
Screen* makeAgents();
Screen* makeAgent(const std::string& id);
Screen* makeNewAgent(const std::string& prompt);
/** Queue of agents waiting for approval. `root` is the Simple mode home: back stays put. */
Screen* makeApprovals(bool root = false);
/** Empty agentId opens a login shell on the host. */
Screen* makeTerminal(const std::string& agentId, const std::string& title);
Screen* makeProcs();
Screen* makeHosts();
Screen* makePairing();
bool pairingVisible();
Screen* makeWifi();
Screen* makeSettings();
Screen* makeSettingsCategory(int idx, int sel);
Screen* makeNotices();
Screen* makeTextView(const std::string& title, const std::string& text);
Screen* makeAbout();
Screen* makeKeyHelp();
/** Progress of the running firmware update (see core/updater.h). */
Screen* makeUpdate();
/** Picks a firmware image on the SD card. Under Launcher it explains the Launcher install instead. */
Screen* makeSdUpdate();
/** SD folder browser: `save` picks a folder for the settings backup, otherwise it picks a backup to restore. */
Screen* makeSdBrowser(bool save);
/** Loads a settings backup from the SD card and rebuilds the screens. Shows a toast on failure. */
void restoreSettingsFile(const std::string& path);
/** Destination picker shared by the home screen and the Opt+Enter shortcut. */
void openLauncher();
