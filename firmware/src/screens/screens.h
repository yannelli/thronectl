#pragma once
#include "../core/ui.h"

Screen* makeSplash();
Screen* makeHome();
Screen* makeAgents();
Screen* makeAgent(const std::string& id);
Screen* makeNewAgent(const std::string& prompt);
/** Empty agentId opens a login shell on the host. */
Screen* makeTerminal(const std::string& agentId, const std::string& title);
Screen* makeProcs();
Screen* makeHosts();
Screen* makePairing();
bool pairingVisible();
Screen* makeWifi();
Screen* makeSettings();
Screen* makeNotices();
Screen* makeTextView(const std::string& title, const std::string& text);
Screen* makeAbout();
/** Progress of the running firmware update (see core/updater.h). */
Screen* makeUpdate();
/** Picks a firmware image on the SD card. Under Launcher it explains the Launcher install instead. */
Screen* makeSdUpdate();
/** Destination picker shared by the home screen and the Opt+Enter shortcut. */
void openLauncher();
