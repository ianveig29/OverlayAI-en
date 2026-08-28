#include "BombInfo.h"
#include "Bomb.h"
#include "imgui.h"
#include "Config.h" // for extern g_Esp
#include "TextFonts.h"

void RenderBombInfoWindow()
{
	if (!g_Esp.showBombInfo) return;
	UpdateBombFromMemory();

	BombState state = g_BombInfo.State();
	bool planted = (state == BombState::Planted || state == BombState::Defusing);

	// 1. Do not show the window at all until a bomb is actually planted
	if (!planted) return;

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
	if (g_Esp.bombInfoAutoResize)
		flags |= ImGuiWindowFlags_AlwaysAutoResize;

	if (GetUiFont()) ImGui::PushFont(GetUiFont());

	ImGui::SetNextWindowSize(ImVec2(320, 0), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Bomb Info", &g_Esp.showBombInfo, flags)) {
		ImGui::End();
		if (GetUiFont()) ImGui::PopFont();
		return;
	}

	double bombTime = g_BombInfo.BombTimeRemaining();
	const char* site = g_BombInfo.SiteLabel();

	// 2. Header / Site
	if (g_Esp.bombInfoShowSite) {
		ImGui::Text("Site:");
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "[ %s ]", site);
		ImGui::SameLine(ImGui::GetWindowWidth() - 110);
		ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "PLANTED");
	}

	// 3. Time Remaining & Explode Progress Bar
	ImVec4 timerColor = (bombTime > 10.0) ? ImVec4(0.2f, 1.0f, 0.3f, 1.0f) :
	                    (bombTime > 5.0)  ? ImVec4(1.0f, 0.75f, 0.1f, 1.0f) :
	                                        ImVec4(1.0f, 0.2f, 0.2f, 1.0f);

	if (g_Esp.bombInfoShowTimer) {
		ImGui::Text("Time:");
		ImGui::SameLine();
		ImGui::TextColored(timerColor, "%s", BombInfo::FormatTimeMinSec(bombTime).c_str());

		float bombProgress = (float)(bombTime / BombInfo::kBombTime);
		if (bombProgress < 0.0f) bombProgress = 0.0f;
		if (bombProgress > 1.0f) bombProgress = 1.0f;

		ImGui::PushStyleColor(ImGuiCol_PlotHistogram, timerColor);
		ImGui::ProgressBar(bombProgress, ImVec2(-1.0f, 6.0f), "");
		ImGui::PopStyleColor();
	}

	// 4. Defuse Information & Progress Bar
	if (state == BombState::Defusing) {
		double defuseTime = g_BombInfo.DefuseTimeRemaining();
		float defuseProgress = g_BombInfo.DefuseProgress();
		bool hasKit = g_BombInfo.DefuseHasKit();
		const std::string& defuserName = g_BombInfo.DefuserName();
		double timeAdvantage = bombTime - defuseTime;

		if (g_Esp.bombInfoShowDefusing) {
			if (g_Esp.bombInfoShowTimer || g_Esp.bombInfoShowSite)
				ImGui::Separator();

			if (!defuserName.empty()) {
				ImGui::Text("Defuser: %s (%s)", defuserName.c_str(), hasKit ? "Kit" : "No Kit");
			} else {
				ImGui::Text("Defusing: (%s)", hasKit ? "Kit" : "No Kit");
			}

			// Defuse progress bar with dynamic color based on progress and safety
			ImVec4 defuseBarColor;
			if (timeAdvantage < 0.0) {
				defuseBarColor = ImVec4(1.0f, 0.2f, 0.2f, 1.0f); // Red if cannot make it
			} else {
				// Smooth color gradient from Orange (starting) -> Cyan (midway) -> Bright Green (finishing)
				if (defuseProgress < 0.5f) {
					float t = defuseProgress * 2.0f;
					defuseBarColor = ImVec4(1.0f - 0.7f * t, 0.6f + 0.3f * t, 0.2f + 0.7f * t, 1.0f);
				} else {
					float t = (defuseProgress - 0.5f) * 2.0f;
					defuseBarColor = ImVec4(0.3f - 0.1f * t, 0.9f + 0.1f * t, 0.9f - 0.6f * t, 1.0f);
				}
			}

			ImGui::PushStyleColor(ImGuiCol_PlotHistogram, defuseBarColor);
			ImGui::ProgressBar(defuseProgress, ImVec2(-1.0f, 0.0f));
			ImGui::PopStyleColor();
		}

		// 5. RUN OR KEEP DEFUSING Decision
		if (g_Esp.bombInfoShowDecision) {
			if (timeAdvantage >= 0.0) {
				ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.3f, 1.0f),
					">>> KEEP DEFUSING! (+%.2fs safe) <<<", timeAdvantage);
			} else {
				ImGui::TextColored(ImVec4(1.0f, 0.15f, 0.15f, 1.0f),
					">>> RUN! WILL EXPLODE! (%.2fs short) <<<", timeAdvantage);
			}
		}
	} else {
		// Not currently defusing
		if (g_Esp.bombInfoShowDecision) {
			if (g_Esp.bombInfoShowTimer || g_Esp.bombInfoShowSite)
				ImGui::Separator();

			if (bombTime >= BombInfo::kDefuseTimeNoKit) {
				ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.3f, 1.0f),
					"Defusable: YES (No kit required, >= 10s)");
			} else if (bombTime >= BombInfo::kDefuseTimeKit) {
				ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.1f, 1.0f),
					"Defusable: ONLY WITH KIT (5s - 10s)");
			} else {
				ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f),
					"Defusable: NO / RUN! (Less than 5s left)");
			}
		}
	}
	if (GetUiFont()) ImGui::PopFont();

	ImGui::End();
}
