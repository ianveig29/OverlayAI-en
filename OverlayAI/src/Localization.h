#pragma once

enum class UiLanguage {
    Spanish = 0,
    English = 1
};

void InitializeLocalization();
UiLanguage GetUiLanguage();
void SetUiLanguage(UiLanguage language);
const char* Localized(const char* spanish, const char* english);

