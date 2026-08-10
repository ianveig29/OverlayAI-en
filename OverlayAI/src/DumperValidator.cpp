// ============================================================
// DumperValidator.cpp
// Validates that dumper data (offsets) is correct and up to date.
// ============================================================

#include "DumperValidator.h"

#include "ConsoleUi.h"
#include "Localization.h"

#include <windows.h>
#include <urlmon.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#pragma comment(lib, "urlmon.lib")

namespace {
    constexpr wchar_t kOfficialDumperUrl[] =
        L"https://github.com/a2x/cs2-dumper/releases/latest/download/cs2-dumper.exe";
    constexpr uintmax_t kMinimumDumperBytes = 256 * 1024;
    constexpr uintmax_t kMaximumDumperBytes = 64 * 1024 * 1024;

    std::filesystem::path GetExecutableDirectory() {
        std::vector<wchar_t> buffer(32768, L'\0');
        const DWORD length = GetModuleFileNameW(
            nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0 || length >= buffer.size())
            return std::filesystem::current_path();
        return std::filesystem::path(
            std::wstring(buffer.data(), length)).parent_path();
    }

    std::filesystem::path NormalizeUserPath(std::string value) {
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
            value = value.substr(1, value.size() - 2);
        return std::filesystem::u8path(value);
    }

    bool IsValidX64PortableExecutable(const std::filesystem::path& path) {
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error)) return false;
        const uintmax_t size = std::filesystem::file_size(path, error);
        if (error || size < kMinimumDumperBytes || size > kMaximumDumperBytes)
            return false;

        std::ifstream file(path, std::ios::binary);
        IMAGE_DOS_HEADER dosHeader{};
        file.read(reinterpret_cast<char*>(&dosHeader), sizeof(dosHeader));
        if (!file || dosHeader.e_magic != IMAGE_DOS_SIGNATURE ||
            dosHeader.e_lfanew <= 0 ||
            static_cast<uintmax_t>(dosHeader.e_lfanew) +
                sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER) > size)
            return false;

        file.seekg(dosHeader.e_lfanew, std::ios::beg);
        DWORD signature = 0;
        IMAGE_FILE_HEADER fileHeader{};
        file.read(reinterpret_cast<char*>(&signature), sizeof(signature));
        file.read(reinterpret_cast<char*>(&fileHeader), sizeof(fileHeader));
        return file && signature == IMAGE_NT_SIGNATURE &&
            fileHeader.Machine == IMAGE_FILE_MACHINE_AMD64;
    }

    bool PublishDumper(
        const std::filesystem::path& source,
        const std::filesystem::path& destination) {
        if (!IsValidX64PortableExecutable(source)) return false;
        const std::filesystem::path staged = destination.wstring() + L".new";
        std::error_code error;
        std::filesystem::copy_file(
            source, staged, std::filesystem::copy_options::overwrite_existing,
            error);
        if (error || !IsValidX64PortableExecutable(staged)) {
            std::filesystem::remove(staged, error);
            return false;
        }
        if (!MoveFileExW(staged.c_str(), destination.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            std::filesystem::remove(staged, error);
            return false;
        }
        return true;
    }

    bool DownloadOfficialDumper(const std::filesystem::path& destination) {
        const std::filesystem::path download =
            destination.wstring() + L".download";
        std::error_code error;
        std::filesystem::remove(download, error);
        const HRESULT result = URLDownloadToFileW(
            nullptr, kOfficialDumperUrl, download.c_str(), 0, nullptr);
        if (FAILED(result) || !PublishDumper(download, destination)) {
            std::filesystem::remove(download, error);
            return false;
        }
        std::filesystem::remove(download, error);
        return true;
    }
}

bool EnsureDumperAvailableInteractive() {
    const std::filesystem::path executableDirectory = GetExecutableDirectory();
    (void)SetCurrentDirectoryW(executableDirectory.c_str());
    const std::filesystem::path dumperPath =
        executableDirectory / L"cs2-dumper.exe";

    if (IsValidX64PortableExecutable(dumperPath)) {
        ConsoleUi::ReportValidatorStatus(
            true, Localized("A2x cs2-dumper encontrado junto a OverlayAI.exe",
                "A2x cs2-dumper found next to OverlayAI.exe"));
        return true;
    }

    ConsoleUi::ReportValidatorStatus(
        false, Localized("cs2-dumper.exe no encontrado o no es un PE x64 valido",
            "cs2-dumper.exe was not found or is not a valid x64 PE"));
    const bool hasExistingCopy = ConsoleUi::PromptValidatorYesNo(
        Localized("Ya tienes una copia oficial de A2x cs2-dumper",
            "Do you already have an official copy of A2x cs2-dumper"));
    if (hasExistingCopy) {
        const std::filesystem::path source = NormalizeUserPath(
            ConsoleUi::PromptValidatorText(Localized(
                "Ruta completa de cs2-dumper.exe",
                "Full path to cs2-dumper.exe")));
        if (PublishDumper(source, dumperPath)) {
            ConsoleUi::ReportValidatorStatus(
                true, Localized("copia validada y preparada junto a OverlayAI.exe",
                    "copy validated and placed next to OverlayAI.exe"));
            return true;
        }
        ConsoleUi::ReportValidatorStatus(
            false, Localized("la copia indicada no es un ejecutable x64 valido",
                "the selected copy is not a valid x64 executable"));
        if (!ConsoleUi::PromptValidatorYesNo(
                Localized("Descargar la ultima release oficial de A2x desde GitHub",
                    "Download the latest official A2x release from GitHub")))
            return false;
    }

    ConsoleUi::ReportValidatorStatus(
        false, Localized("descargando release oficial de a2x/cs2-dumper...",
            "downloading the official a2x/cs2-dumper release..."));
    if (!DownloadOfficialDumper(dumperPath)) {
        ConsoleUi::ReportValidatorStatus(
            false, Localized("la descarga fallo; se intentara usar la cache validada",
                "download failed; the validated cache will be used if available"));
        return false;
    }
    ConsoleUi::ReportValidatorStatus(
        true, Localized("release oficial descargada y validada como PE x64",
            "official release downloaded and validated as an x64 PE"));
    return true;
}
