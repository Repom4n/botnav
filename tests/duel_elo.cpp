#include "g_duel_elo.h"

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE( duel_elo )

BOOST_AUTO_TEST_CASE( human_wins_against_each_bot_seed_gain_rating )
{
	for (int level = 1; level <= 10; ++level)
	{
		const float botElo = 800.0f + (level - 1) * 300.0f / 9.0f;
		float humanAfter, botAfter;
		const float odds = G_CalculateDuelElo(1000.0f, botElo, 25.0f, 25.0f, &humanAfter, &botAfter);

		BOOST_CHECK_GT(humanAfter, 1000.0f);
		BOOST_CHECK_LT(botAfter, botElo);
		BOOST_CHECK_GT(odds, 0.0f);
		BOOST_CHECK_LT(odds, 1.0f);

		float humanAgain, botAgain;
		G_CalculateDuelElo(humanAfter, botAfter, 25.0f, 25.0f, &humanAgain, &botAgain);
		BOOST_CHECK_GT(humanAgain, humanAfter);
		BOOST_CHECK_LT(botAgain, botAfter);
	}
}

BOOST_AUTO_TEST_CASE( fractional_provisional_k_does_not_erase_gains )
{
	float winnerAfter, loserAfter;
	G_CalculateDuelElo(1000.0f, 1000.0f, 1.0f * 0.1f, 1.0f * 0.1f, &winnerAfter, &loserAfter);
	BOOST_CHECK_CLOSE(winnerAfter, 1000.05f, 0.0001f);
	BOOST_CHECK_CLOSE(loserAfter, 999.95f, 0.0001f);
}

BOOST_AUTO_TEST_CASE( equal_ratings_and_upsets_keep_standard_elo_behavior )
{
	float winnerAfter, loserAfter;
	const float odds = G_CalculateDuelElo(1000.0f, 1000.0f, 25.0f, 25.0f, &winnerAfter, &loserAfter);
	BOOST_CHECK_EQUAL(odds, 0.5f);
	BOOST_CHECK_EQUAL(winnerAfter, 1012.5f);
	BOOST_CHECK_EQUAL(loserAfter, 987.5f);

	G_CalculateDuelElo(800.0f, 1200.0f, 25.0f, 25.0f, &winnerAfter, &loserAfter);
	BOOST_CHECK_CLOSE(winnerAfter, 822.7273f, 0.001f);
	BOOST_CHECK_CLOSE(loserAfter, 1177.2727f, 0.001f);
}

BOOST_AUTO_TEST_CASE( high_ratings_do_not_overflow_expected_scores )
{
	float winnerAfter, loserAfter;
	const float odds = G_CalculateDuelElo(20000.0f, 20000.0f, 25.0f, 25.0f, &winnerAfter, &loserAfter);
	BOOST_CHECK_EQUAL(odds, 0.5f);
	BOOST_CHECK_EQUAL(winnerAfter, 20012.5f);
	BOOST_CHECK_EQUAL(loserAfter, 19987.5f);
}

BOOST_AUTO_TEST_SUITE_END()
