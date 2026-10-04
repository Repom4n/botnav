#pragma once

#include <math.h>

//Keep rating arithmetic independent of the engine for regression tests.
static inline float G_CalculateDuelElo(float winnerElo, float loserElo,
	float winnerK, float loserK, float *newWinnerElo, float *newLoserElo)
{
	const double expectedScoreLoser = 1.0 / (1.0 + pow(10.0, (winnerElo - loserElo) / 400.0));

	*newWinnerElo = winnerElo + winnerK * expectedScoreLoser;
	*newLoserElo = loserElo - loserK * expectedScoreLoser;
	return (float)(1.0 - expectedScoreLoser);
}
