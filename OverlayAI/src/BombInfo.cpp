#include "BombInfo.h"
#include "Bomb.h"
#include "imgui.h"
#include "Config.h" // for extern g_Esp

void RenderBombInfoWindow()
{
	if (!g_Esp.showBombInfo) return;
	UpdateBombFromMemory();
	ImGui::SetNextWindowSize(ImVec2(380, 96), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Bomb Info", &g_Esp.showBombInfo, ImGuiWindowFlags_NoCollapse)) {
		ImGui::End();
		return;
	}

	BombState state = g_BombInfo.State();
	bool planted = (state == BombState::Planted || state == BombState::Defusing);
	ImGui::Text("Planted: %s", planted ? "Yes" : "No");

	if (planted) {
		ImGui::Text("Time: %s", BombInfo::FormatTimeMinSec(g_BombInfo.BombTimeRemaining()).c_str());
		ImGui::Text("Site: %s", g_BombInfo.SiteLabel());
		if (state == BombState::Defusing) {
			ImGui::Text("Defusing: Yes (%s)", g_BombInfo.DefuseHasKit() ? "Kit" : "No kit");
			ImGui::Text("Defuse Time: %s", BombInfo::FormatTime(g_BombInfo.DefuseTimeRemaining()).c_str());
			ImGui::ProgressBar(g_BombInfo.DefuseProgress(), ImVec2(-1.0f, 0.0f));
		} else {
			ImGui::Text("Defusing: No");
		}
	}

	ImGui::End();
}
