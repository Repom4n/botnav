#pragma once

//Pure helpers for the duel ELO ladder identity. Kept free of engine dependencies so the key
//format can be unit tested.

#include <stdio.h>
#include <string.h>

#define G_DUEL_GUEST_KEY_PREFIX "iphash:"

#define G_DUEL_BOT_LEVEL_KEY_PREFIX "botlvl"

//Builds the ladder key for a bot. Rated bots share one rating per skill level ("botlvl5"),
//so every bot played at level 5 earns ELO for that level and the ladder shows one entry per
//level. The bot file (e.g. "botfiles/mediumds.jkb" -> "mediumds") or netname is only used
//when no level is known. Returns 1 when a key was written.
static inline int G_FormatBotDuelIdentity(const char *botFile, const char *fallbackName, int botLevel, char *out, size_t outSize)
{
	char baseName[64];
	const char *source = NULL;
	const char *slash;
	const char *backslash;
	char *dot;

	if (!out || outSize < 1)
		return 0;
	out[0] = '\0';
	baseName[0] = '\0';

	if (botLevel > 0)
	{
		snprintf(out, outSize, "%s%i", G_DUEL_BOT_LEVEL_KEY_PREFIX, botLevel);
		return 1;
	}

	if (botFile && botFile[0])
	{
		source = botFile;
		slash = strrchr(botFile, '/');
		backslash = strrchr(botFile, '\\');
		if (backslash && (!slash || backslash > slash))
			slash = backslash;
		if (slash)
			source = slash + 1;
		snprintf(baseName, sizeof(baseName), "%s", source);
		dot = strrchr(baseName, '.');
		if (dot && dot != baseName)
			*dot = '\0';
	}
	if (!baseName[0] && fallbackName && fallbackName[0])
		snprintf(baseName, sizeof(baseName), "%s", fallbackName);
	if (!baseName[0])
		return 0;

	snprintf(out, outSize, "%s", baseName);
	return 1;
}

//Unregistered humans only get a rated identity (their stable "iphash:" key) when they duel
//a bot, so bots' wins and losses against guests count without filling the ladder with
//guest-vs-guest results.
static inline int G_ShouldUseGuestDuelKey(int hasOwnKey, int isBot, int opponentIsBot, int opponentHasKey)
{
	return (!hasOwnKey && !isBot && opponentIsBot && opponentHasKey) ? 1 : 0;
}

static inline int G_IsGuestDuelKey(const char *name)
{
	return (name && !strncmp(name, G_DUEL_GUEST_KEY_PREFIX, strlen(G_DUEL_GUEST_KEY_PREFIX))) ? 1 : 0;
}
