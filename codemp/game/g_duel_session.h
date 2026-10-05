#pragma once

#include "sqlite3.h"

//The bundled SQLite PRNG requires an initialized mutex subsystem.
static inline int G_GenerateLearningSessionNonce(unsigned char nonce[16])
{
	const int result = sqlite3_initialize();
	if (result != SQLITE_OK)
		return result;
	sqlite3_randomness(16, nonce);
	return SQLITE_OK;
}
