# -*- coding: utf-8 -*-
def rep(d, old, new):
    n = d.count(old)
    if n == 1:
        return d.replace(old, new)
    old_c = old.replace('\n', '\r\n')
    if d.count(old_c) == 1:
        return d.replace(old_c, new.replace('\n', '\r\n'))
    raise AssertionError(f'anchor not unique: LF={n} CRLF={d.count(old_c)}: {old[:50]!r}')
def load(p): return open(p, 'rb').read().decode('utf-8')
def save(p, d): open(p, 'wb').write(d.encode('utf-8'))

# ---------- Types.h ----------
p = 'OverlayAI/src/Types.h'; d = load(p)
anchor = 'int antiViewPunchDecay = 999;'
assert d.count(anchor) == 1
d = rep(d, anchor, anchor + '''
};

// Native chat: REAL in-game messages typed via keyboard simulation
// (clipboard + Ctrl+V). See ChatSpam.h for the full explanation.
struct ChatSpamSettings {
    bool spamEnabled = false;      // message loop with interval
    int intervalSeconds = 5;      // seconds between messages (minimum 2)
    bool useTeamChat = false;     // false = all chat (U), true = team (Y)
    char messages[4][128] = {};   // loop messages (empty = skipped)
    bool killSayEnabled = false;  // send a message on kill
    char killSayMessage[128] = {}; // the kill say message''')
save(p, d); print('Types.h ok')

# ---------- Config.h ----------
p = 'OverlayAI/src/Config.h'; d = load(p)
anchor = 'extern AimSettings g_Aim;'
assert d.count(anchor) == 1
d = rep(d, anchor, anchor + '''
extern struct ChatSpamSettings g_ChatSpam;''')
save(p, d); print('Config.h ok')

# ---------- Config.cpp ----------
p = 'OverlayAI/src/Config.cpp'; d = load(p)
anchor = 'AimSettings g_Aim;'
assert d.count(anchor) == 1
d = rep(d, anchor, anchor + '''
ChatSpamSettings g_ChatSpam;''')
# save (despues del fprintf de antiviewpunch_decay)
anchor = 'fprintf(f, "antiviewpunch_decay=%d\\n", g_Aim.antiViewPunchDecay);'
assert d.count(anchor) == 1
d = rep(d, anchor, anchor + '''
    fprintf(f, "chatspam_enabled=%d\\n", g_ChatSpam.spamEnabled ? 1 : 0);
    fprintf(f, "chatspam_interval=%d\\n", g_ChatSpam.intervalSeconds);
    fprintf(f, "chatspam_team=%d\\n", g_ChatSpam.useTeamChat ? 1 : 0);
    for (int i = 0; i < 4; ++i)
        fprintf(f, "chatspam_msg%d=%s\\n", i + 1, g_ChatSpam.messages[i]);
    fprintf(f, "killsay_enabled=%d\\n", g_ChatSpam.killSayEnabled ? 1 : 0);
    fprintf(f, "killsay_msg=%s\\n", g_ChatSpam.killSayMessage);''')
# load: insertar el bloque de chat ANTES del ancla rcs_enabled (patron correcto)
anchor = '} else if (sscanf_s(p, "rcs_enabled=%d", &i1) == 1) {\n            g_Aim.recoilControlSystem = i1 != 0;'
d = rep(d, anchor, '''        } else if (sscanf_s(p, "chatspam_enabled=%d", &i1) == 1) {
            g_ChatSpam.spamEnabled = i1 != 0;
        } else if (sscanf_s(p, "chatspam_interval=%d", &i1) == 1) {
            if (i1 >= 2 && i1 <= 60) g_ChatSpam.intervalSeconds = i1;
        } else if (sscanf_s(p, "chatspam_team=%d", &i1) == 1) {
            g_ChatSpam.useTeamChat = i1 != 0;
        } else if (strncmp(p, "chatspam_msg1=", 14) == 0) {
            (void)sscanf_s(p, "chatspam_msg1=%127[^\\r\\n]", g_ChatSpam.messages[0], (unsigned)sizeof(g_ChatSpam.messages[0]));
        } else if (strncmp(p, "chatspam_msg2=", 14) == 0) {
            (void)sscanf_s(p, "chatspam_msg2=%127[^\\r\\n]", g_ChatSpam.messages[1], (unsigned)sizeof(g_ChatSpam.messages[1]));
        } else if (strncmp(p, "chatspam_msg3=", 14) == 0) {
            (void)sscanf_s(p, "chatspam_msg3=%127[^\\r\\n]", g_ChatSpam.messages[2], (unsigned)sizeof(g_ChatSpam.messages[2]));
        } else if (strncmp(p, "chatspam_msg4=", 14) == 0) {
            (void)sscanf_s(p, "chatspam_msg4=%127[^\\r\\n]", g_ChatSpam.messages[3], (unsigned)sizeof(g_ChatSpam.messages[3]));
        } else if (sscanf_s(p, "killsay_enabled=%d", &i1) == 1) {
            g_ChatSpam.killSayEnabled = i1 != 0;
        } else if (strncmp(p, "killsay_msg=", 12) == 0) {
            (void)sscanf_s(p, "killsay_msg=%127[^\\r\\n]", g_ChatSpam.killSayMessage, (unsigned)sizeof(g_ChatSpam.killSayMessage));
        } else if (sscanf_s(p, "rcs_enabled=%d", &i1) == 1) {
            g_Aim.recoilControlSystem = i1 != 0;''')
save(p, d); print('Config.cpp ok')

# ---------- Offsets.h ----------
p = 'OverlayAI/src/Offsets.h'; d = load(p)
anchor = 'extern uintptr_t dwViewPunchDecayConVar;'
assert d.count(anchor) == 1
d = rep(d, anchor, anchor + '''
// Kill say chain: controller -> ActionTrackingServices -> round kills.
extern uintptr_t m_pActionTrackingServices;
extern uintptr_t m_iNumRoundKills;''')
save(p, d); print('Offsets.h ok')

# ---------- Offsets.cpp ----------
p = 'OverlayAI/src/Offsets.cpp'; d = load(p)
anchor = 'uintptr_t dwViewPunchDecayConVar = 0x239CE58; // pointer to the view_punch_decay ConVar (CE 12/09/2026)'
d = rep(d, anchor, anchor + '''
uintptr_t m_pActionTrackingServices = 0x820; // CCSPlayerController::m_pActionTrackingServices (schema dump)
uintptr_t m_iNumRoundKills = 0x128; // CCSPlayerController_ActionTrackingServices::m_iNumRoundKills (schema dump)''')
anchor = 'if (ks == "dwViewPunchDecayConVar") { Offsets::dwViewPunchDecayConVar = v; return; }'
d = rep(d, anchor, anchor + '''
    if (ks == "m_pActionTrackingServices") { Offsets::m_pActionTrackingServices = v; return; }
    if (ks == "m_iNumRoundKills") { Offsets::m_iNumRoundKills = v; return; }''')
anchor = '"dwThirdPersonValue", "dwViewPunchDecayConVar",'
d = rep(d, anchor, '"dwThirdPersonValue", "dwViewPunchDecayConVar", "m_pActionTrackingServices", "m_iNumRoundKills",')
save(p, d); print('Offsets.cpp ok')

# ---------- offsets.json ----------
p = 'OverlayAI/offsets.json'; d = load(p)
import re
m = re.search(r'"dwViewPunchDecayConVar":\s*(\d+)', d)
assert m
d = rep(d, m.group(0), m.group(0) + ',\n    "m_pActionTrackingServices": 2080,\n    "m_iNumRoundKills": 296')
save(p, d); print('offsets.json ok')

# ---------- Main.cpp ----------
p = 'OverlayAI/src/Main.cpp'; d = load(p)
anchor = '''        // Anti View Punch: re-asserts the ConVar decay every frame.
        UpdateAntiViewPunch();'''
d = rep(d, anchor, anchor + '''

        // Native chat: state machine for the spammer and the kill say.
        UpdateChatSpam();''')
anchor = '#include "ViewPunch.h"'
d = rep(d, anchor, anchor + '\n#include "ChatSpam.h"')
save(p, d); print('Main.cpp ok')

# ---------- Menu.cpp ----------
p = 'OverlayAI/src/Menu.cpp'; d = load(p)
anchor = '''                ImGui::Checkbox(Localized("Perfil falso##fake_profile",
                    "Fake profile##fake_profile"), &g_Esp.fakeProfile);'''
d = rep(d, anchor, anchor + '''

                ImGui::Separator();
                ImGui::TextUnformatted(Localized("Chat nativo",
                    "Native chat"));
                ImGui::Checkbox(Localized("Spam de mensajes##cs_enabled",
                    "Message spam##cs_enabled"), &g_ChatSpam.spamEnabled);
                if (g_ChatSpam.spamEnabled) {
                    ImGui::Indent(20.0f);
                    ImGui::SliderInt(Localized("Intervalo (segundos)##cs_interval",
                        "Interval (seconds)##cs_interval"), &g_ChatSpam.intervalSeconds, 2, 60);
                    const char* chatOptions[] = { Localized("Todos (U)", "All chat (U)"),
                        Localized("Equipo (Y)", "Team (Y)") };
                    int chatKind = g_ChatSpam.useTeamChat ? 1 : 0;
                    ImGui::Combo(Localized("Destino##cs_chat", "Target##cs_chat"),
                        &chatKind, chatOptions, 2);
                    g_ChatSpam.useTeamChat = chatKind != 0;
                    for (int i = 0; i < 4; ++i) {
                        char label[32];
                        sprintf_s(label, Localized("Mensaje %d##cs_msg%d",
                            "Message %d##cs_msg%d"), i + 1, i + 1);
                        ImGui::InputText(label, g_ChatSpam.messages[i],
                            sizeof(g_ChatSpam.messages[i]));
                    }
                    ImGui::Unindent(20.0f);
                }
                ImGui::Checkbox(Localized("Mensaje al matar (kill say)##cs_killsay",
                    "Kill say message##cs_killsay"), &g_ChatSpam.killSayEnabled);
                if (g_ChatSpam.killSayEnabled) {
                    ImGui::Indent(20.0f);
                    ImGui::InputText(Localized("Mensaje##cs_killsay_msg",
                        "Message##cs_killsay_msg"), g_ChatSpam.killSayMessage,
                        sizeof(g_ChatSpam.killSayMessage));
                    ImGui::Unindent(20.0f);
                }
                {
                    const ChatSpamStatus cs = GetChatSpamStatus();
                    if (!cs.gameFocused)
                        ImGui::TextDisabled(Localized("Estado: esperando el foco del juego",
                            "Status: waiting for the game window focus"));
                    else if (cs.busy)
                        ImGui::Text(Localized("Estado: escribiendo...",
                            "Status: typing..."));
                    else
                        ImGui::Text(Localized("Estado: listo | enviados: %d | kills: %d",
                            "Status: ready | sent: %d | kills: %d"),
                            cs.messagesSent, cs.killsDetected);
                }
                ImGui::TextDisabled(Localized(
                    "Escribe en el chat REAL del juego simulando teclado (portapapeles + Ctrl+V). Solo envia teclas con la ventana del juego en foco y el menu cerrado.",
                    "Writes to the REAL in-game chat by simulating keyboard (clipboard + Ctrl+V). Keys are only sent while the game window is focused and the menu is closed."));''')
anchor = '#include "ViewPunch.h"'
d = rep(d, anchor, anchor + '\n#include "ChatSpam.h"')
save(p, d); print('Menu.cpp ok')

# ---------- vcxproj ----------
p = 'OverlayAI/OverlayAI.vcxproj'; d = load(p)
old = '<ClCompile Include="src\\ViewPunch.cpp" />'
assert d.count(old) == 1
d = d.replace(old, old + '\r\n    <ClCompile Include="src\\ChatSpam.cpp" />')
old = '<ClInclude Include="src\\ViewPunch.h" />'
assert d.count(old) == 1
d = d.replace(old, old + '\r\n    <ClInclude Include="src\\ChatSpam.h" />')
open(p, 'wb').write(d.encode('utf-8')); print('vcxproj ok')
