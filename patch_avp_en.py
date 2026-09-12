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
anchor = 'int rcsStrengthPercent = 100;'
assert d.count(anchor) == 1
d = rep(d, anchor, anchor + '''
    // Anti View Punch: speeds up how fast the camera kick from damage
    // dissolves (view_punch_decay ConVar). It does not fight the punch
    // system: it just makes the kick disappear almost instantly.
    bool enableAntiViewPunch = false;
    // Target decay while the feature is active (999 = instant).
    int antiViewPunchDecay = 999;''')
save(p, d); print('Types.h ok')

# ---------- Offsets.h ----------
p = 'OverlayAI/src/Offsets.h'; d = load(p)
anchor = 'extern uintptr_t dwThirdPersonValue;'
assert d.count(anchor) == 1
d = rep(d, anchor, anchor + '''
// Static pointer in client.dll to the view_punch_decay ConVar object
// (custom key from 12/09/2026: the dumper does not provide it; CE recovery in ViewPunch.h).
extern uintptr_t dwViewPunchDecayConVar;''')
save(p, d); print('Offsets.h ok')

# ---------- Offsets.cpp ----------
p = 'OverlayAI/src/Offsets.cpp'; d = load(p)
anchor = 'uintptr_t dwThirdPersonValue = 0x23E2838;'
assert d.count(anchor) == 1
d = rep(d, anchor, anchor + '''
uintptr_t dwViewPunchDecayConVar = 0x239CE58; // pointer to the view_punch_decay ConVar (CE 12/09/2026)''')
anchor = 'if (ks == "dwThirdPersonValue") { Offsets::dwThirdPersonValue = v; return; }'
assert d.count(anchor) == 1
d = rep(d, anchor, anchor + '''
    if (ks == "dwViewPunchDecayConVar") { Offsets::dwViewPunchDecayConVar = v; return; }''')
anchor = '"dwThirdPersonPatch", "dwThirdPersonValue",'
assert d.count(anchor) == 1
d = rep(d, anchor, anchor + ' "dwViewPunchDecayConVar",')
anchor = '{ "dwThirdPersonValue", Offsets::dwThirdPersonValue, Group::Core },'
assert d.count(anchor) == 1
d = rep(d, anchor, anchor + '''
        { "dwViewPunchDecayConVar", Offsets::dwViewPunchDecayConVar, Group::Core },''')
save(p, d); print('Offsets.cpp ok')

# ---------- offsets.json ----------
p = 'OverlayAI/offsets.json'; d = load(p)
import re
m = re.search(r'"dwThirdPersonValue":\s*(\d+)', d)
assert m
d = rep(d, m.group(0), m.group(0) + ',\n    "dwViewPunchDecayConVar": 37342808')
save(p, d); print('offsets.json ok')

# ---------- Config.cpp ----------
p = 'OverlayAI/src/Config.cpp'; d = load(p)
anchor = 'fprintf(f, "rcs_enabled=%d\\n", g_Aim.recoilControlSystem ? 1 : 0);'
assert d.count(anchor) == 1
d = rep(d, anchor, anchor + '''
    fprintf(f, "antiviewpunch_enabled=%d\\n", g_Aim.enableAntiViewPunch ? 1 : 0);
    fprintf(f, "antiviewpunch_decay=%d\\n", g_Aim.antiViewPunchDecay);''')
anchor = '} else if (sscanf_s(p, "rcs_enabled=%d", &i1) == 1) {'
assert d.count(anchor) == 1
d = rep(d, anchor, anchor + '''
        } else if (sscanf_s(p, "antiviewpunch_enabled=%d", &i1) == 1) {
            g_Aim.enableAntiViewPunch = (i1 != 0);
        } else if (sscanf_s(p, "antiviewpunch_decay=%d", &i1) == 1) {
            if (i1 >= 20 && i1 <= 999) g_Aim.antiViewPunchDecay = i1;''')
save(p, d); print('Config.cpp ok')

# ---------- Main.cpp ----------
p = 'OverlayAI/src/Main.cpp'; d = load(p)
anchor = '''        // RCS: compensates recoil into the view angles every frame.
        RunRCS();'''
d = rep(d, anchor, anchor + '''

        // Anti View Punch: re-asserts the ConVar decay every frame.
        UpdateAntiViewPunch();''')
anchor = '#include "RecoilControl.h"'
d = rep(d, anchor, anchor + '\n#include "ViewPunch.h"')
anchor = '\n    RestoreMoneyReveal();\n'
assert d.count(anchor) == 1
d = d.replace(anchor, '\n    RestoreMoneyReveal();\n    ShutdownAntiViewPunch();\n')
save(p, d); print('Main.cpp ok')

# ---------- Menu.cpp ----------
p = 'OverlayAI/src/Menu.cpp'; d = load(p)
anchor = '''                if (g_Aim.recoilControlSystem)
                    ImGui::Text("Frames compensated: %u", GetRCSCompensationCount());'''
d = rep(d, anchor, anchor + '''

                ImGui::Separator();
                ImGui::TextUnformatted(Localized("Anti View Punch",
                    "Anti View Punch"));
                ImGui::Checkbox(Localized("Kick de camara instantaneo##avp_enabled",
                    "Instant camera kick decay##avp_enabled"), &g_Aim.enableAntiViewPunch);
                ImGui::SliderInt(Localized("Decay objetivo##avp_decay",
                    "Target decay##avp_decay"), &g_Aim.antiViewPunchDecay, 20, 999, "%.0f");
                ImGui::TextDisabled(Localized(
                    "Acelera cuanto tarda en disolverse la patada de camara al recibir dano (ConVar view_punch_decay). OJO en MM: el server conserva su propio decay, probalo primero en bots.",
                    "Speeds up how fast the camera kick from damage dissolves (view_punch_decay ConVar). WARNING in MM: the server keeps its own decay, test on bots first."));
                {
                    const AntiViewPunchStatus avp = GetAntiViewPunchStatus();
                    if (avp.failReason == 2) {
                        ImGui::TextColored(ImVec4(1.f, 0.2f, 0.2f, 1.f),
                            Localized("Estado: offset vencido (update del juego?) - ver ViewPunch.h",
                                "Status: stale offset (game update?) - see ViewPunch.h"));
                    } else if (avp.failReason == 3) {
                        ImGui::TextColored(ImVec4(1.f, 0.2f, 0.2f, 1.f),
                            Localized("Estado: fallo la escritura en memoria",
                                "Status: memory write failed"));
                    } else if (avp.failReason == 4) {
                        ImGui::TextColored(ImVec4(1.f, 0.8f, 0.2f, 1.f),
                            Localized("Estado: lectura invalida, reintentando",
                                "Status: invalid read, retrying"));
                    } else if (avp.failReason == 1) {
                        ImGui::TextDisabled(Localized("Estado: esperando el juego...",
                            "Status: waiting for the game..."));
                    } else if (avp.originalValue > 0.f) {
                        ImGui::Text(Localized("Decay actual: %.0f (original %.0f)%s",
                            "Current decay: %.0f (original %.0f)%s"),
                            avp.currentValue, avp.originalValue,
                            avp.active ? Localized(" [activo]", " [active]") : "");
                    }
                }''')
anchor = '#include "RecoilControl.h"'
d = rep(d, anchor, anchor + '\n#include "ViewPunch.h"')
save(p, d); print('Menu.cpp ok')

# ---------- vcxproj ----------
p = 'OverlayAI/OverlayAI.vcxproj'; d = load(p)
old = '<ClCompile Include="src\\ThirdPerson.cpp" />'
assert d.count(old) == 1
d = d.replace(old, old + '\r\n    <ClCompile Include="src\\ViewPunch.cpp" />')
old = '<ClInclude Include="src\\ThirdPerson.h" />'
assert d.count(old) == 1
d = d.replace(old, old + '\r\n    <ClInclude Include="src\\ViewPunch.h" />')
open(p, 'wb').write(d.encode('utf-8')); print('vcxproj ok')
