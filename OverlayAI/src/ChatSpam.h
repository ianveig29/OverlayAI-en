#pragma once

// ============================================================================
// Native chat -real in-game messages + spammer + kill say-
// ============================================================================
//
// WHAT THIS MODULE DOES:
// Sends messages to the game's REAL chat (the one every player sees),
// without injecting anything: it simulates you typing. Two modes:
//
// 1) SPAMMER: sends a list of your own messages in a loop, with a
//    configurable interval between messages.
// 2) KILL SAY: sends an automatic message every time you kill someone.
//
// HOW IT WORKS INSIDE:
// There is no "clean" way to write into the chat from outside the
// process (the VConsole port is closed in the game's normal mode, and
// hand-crafted network packets would be detectable). What we do is what
// a human does: keyboard simulation. We put the message into the
// Windows clipboard, open the game chat (Y = team, U = all), paste with
// Ctrl+V and press Enter. All through SendInput.
//
// MODULE SAFETY RULES:
// - Keys are only sent while the GAME WINDOW has focus (if the overlay
//   menu is open or you are in Discord/Steam, nothing is typed).
// - While the game is in focus, YOU can type too: if we are mid-sequence
//   it completes in ~160ms and it is done.
// - The minimum interval is 2 seconds: the game chat has a per-minute
//   message limit and going over it can mute the chat for a while.
// ============================================================================

struct ChatSpamStatus {
    int  phase = 0;         // 0=idle, 1=opening chat, 2=pasting, 3=enter, 4=waiting
    int  messagesSent = 0;  // messages sent in this session
    int  killsDetected = 0; // kills detected in this session
    bool busy = false;      // true if a sequence is in progress
    bool gameFocused = false; // true if the game window has focus
};

void UpdateChatSpam();       // call every frame of the main loop
ChatSpamStatus GetChatSpamStatus(); // status for the menu line
