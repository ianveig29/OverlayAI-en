// ============================================================================
// CHAT WHEEL UNBOX (EXPERIMENTAL MODULE)
// ----------------------------------------------------------------------------
// Implements the connection to CS2's VConsole (Source 2 official remote
// console) to run the "playerchatwheel" command with a fake unbox message.
//
// VCONSOLE PROTOCOL (researched from CS2RemoteConsole / libvconsole,
// MIT license, theokyr):
// - The game listens on TCP 127.0.0.1:29000.
// - Every message ("chunk") looks like this:
//     bytes 0-3   : message type, e.g. "CMND" (command to run)
//     bytes 4-7   : protocol version (fixed: 00 D4 00 00)
//     bytes 8-9   : TOTAL chunk length (big-endian: high byte first)
//     bytes 10-11 : handle (fixed: 00 00)
//     rest        : command text + terminating 0x00 byte
// - VConsole answers with PRNT messages (console output), CHAN, etc.
//   This module does not need to read the answer.
// ============================================================================

// Include winsock2 BEFORE any windows.h to avoid socket version
// conflicts. WIN32_LEAN_AND_MEAN helps too.
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

// Links the Windows sockets library without touching the .vcxproj
#pragma comment(lib, "Ws2_32.lib")

#include "ChatWheelUnbox.h"

#include <cstdint>
#include <string>
#include <vector>

namespace {

    // Fixed address and port of the game remote console.
    constexpr const char* kVconsoleAddress = "127.0.0.1";
    constexpr int kVconsolePort = 29000;

    // Builds the full "CMND" chunk with the command inside.
    // (Exact replica of the format used by the official vconsole2 tool)
    std::vector<unsigned char> BuildCommandChunk(const std::string& command) {
        // 12-byte header. The length (bytes 8-9) is filled later.
        std::vector<unsigned char> chunk = {
            'C', 'M', 'N', 'D',          // message type: command
            0x00, 0xD4, 0x00, 0x00,      // protocol version
            0x00, 0x18,                  // total length (placeholder, overwritten)
            0x00, 0x00                   // handle
        };

        // Append the command text and the terminating 0x00 byte.
        chunk.insert(chunk.end(), command.begin(), command.end());
        chunk.push_back(0x00);

        // Write the TOTAL chunk length into bytes 8-9 in big-endian
        // (high byte first, then low byte).
        const auto total = static_cast<unsigned short>(chunk.size());
        chunk[8] = static_cast<unsigned char>((total >> 8) & 0xFF);
        chunk[9] = static_cast<unsigned char>(total & 0xFF);

        return chunk;
    }

    // Initializes the sockets library only once.
    bool EnsureWinsockStarted() {
        static bool done = false;
        static bool ok = false;
        if (!done) {
            WSADATA data{};
            ok = WSAStartup(MAKEWORD(2, 2), &data) == 0;
            done = true;
        }
        return ok;
    }

} // namespace

namespace ChatWheelUnbox {

    Result SendFakeUnbox(const char* itemText) {
        if (!itemText || !EnsureWinsockStarted())
            return Result::ConnectFailed;

        // Strip quotes from the item text so they don't break the command.
        std::string item;
        for (const char* p = itemText; *p; ++p)
            if (*p != '"')
                item += *p;

        // The game's native command: sends a chat wheel phrase
        // (CW.WePlanted) but replaces the text with the fake unbox
        // message, in the same format the game really uses.
        std::string command =
            "playerchatwheel CW.WePlanted \" has opened a container and found: ";
        command += item;
        command += "\"";

        // 1. Create the TCP socket.
        const SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCKET)
            return Result::ConnectFailed;

        // 2. Prepare the 127.0.0.1:29000 address.
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(static_cast<unsigned short>(kVconsolePort));
        inet_pton(AF_INET, kVconsoleAddress, &address.sin_addr);

        // 3. Connect. If the game does not have VConsole open
        //    (it usually requires launching with -tools) this fails fast.
        if (connect(sock, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
            closesocket(sock);
            return Result::ConnectFailed;
        }

        // 4. Build the CMND chunk and send it.
        const std::vector<unsigned char> chunk = BuildCommandChunk(command);
        const int sent = send(sock, reinterpret_cast<const char*>(chunk.data()),
                               static_cast<int>(chunk.size()), 0);

        // 5. Close. The command is already queued in the game console.
        shutdown(sock, SD_BOTH);
        closesocket(sock);

        return sent == SOCKET_ERROR ? Result::ConnectFailed : Result::Sent;
    }

} // namespace ChatWheelUnbox
