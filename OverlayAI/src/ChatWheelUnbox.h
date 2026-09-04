#pragma once

// ============================================================================
// CHAT WHEEL UNBOX (EXPERIMENTAL MODULE)
// ----------------------------------------------------------------------------
// Sends a fake "you opened a case and got X" message to your team chat,
// using the game's native "playerchatwheel" console command.
//
// HOW IT WORKS (summary for non-programmers):
// - CS2 has an official remote console (VConsole) listening on
//   TCP port 29000 of localhost (127.0.0.1).
// - We connect to that port and send the "playerchatwheel" command
//   with the fake unbox text.
// - The game runs it as if you typed it in the console.
//
// IMPORTANT: the VConsole socket may require launching CS2 with the
// "-tools" option. If the connection fails, that is the reason.
//
// HOW TO REMOVE THIS MODULE (if it goes stale or stops working):
// 1. Delete ChatWheelUnbox.cpp and ChatWheelUnbox.h
// 2. Delete the "EXPERIMENTAL MODULE" block in Menu.cpp (Utility tab)
// 3. Delete the 2 ChatWheelUnbox entries in OverlayAI.vcxproj
// It does not touch any other file of the project.
// ============================================================================

namespace ChatWheelUnbox {

    // Result of the last send attempt.
    enum class Result {
        Sent,           // The command was sent through the VConsole socket
        ConnectFailed   // Could not connect (VConsole is not listening)
    };

    // Sends the fake unbox message to the team chat.
    // itemText: item name, e.g. "Karambit | Doppler"
    Result SendFakeUnbox(const char* itemText);

} // namespace ChatWheelUnbox
