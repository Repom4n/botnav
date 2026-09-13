#include "ai_combat_tuning.h"

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE( bot_ai )

BOOST_AUTO_TEST_SUITE( tuning )

BOOST_AUTO_TEST_CASE( ptk_armor_penalty_tracks_force_lead )
{
	BOOST_CHECK_EQUAL( NewBotAI_AdjustPTKWeightForArmor( 50, 0, 0 ), 50 );
	BOOST_CHECK_EQUAL( NewBotAI_AdjustPTKWeightForArmor( 50, 25, -5 ), -10 );
	BOOST_CHECK_EQUAL( NewBotAI_AdjustPTKWeightForArmor( 50, 25, 10 ), 15 );
	BOOST_CHECK_EQUAL( NewBotAI_AdjustPTKWeightForArmor( 50, 25, 25 ), 60 );
}

BOOST_AUTO_TEST_CASE( drainlock_force_choice_prefers_pull_when_it_matches_or_beats_drain )
{
	BOOST_CHECK_EQUAL(
		NewBotAI_GetDrainlockForceChoice( 0, 1, 0, 70, 70, 40 ),
		NEWBOTAI_DRAINLOCK_FORCE_PULL );
	BOOST_CHECK_EQUAL(
		NewBotAI_GetDrainlockForceChoice( 0, 0, 1, 90, 80, 10 ),
		NEWBOTAI_DRAINLOCK_FORCE_PULL );
}

BOOST_AUTO_TEST_CASE( drainlock_force_choice_falls_back_to_drain_when_pull_loses )
{
	BOOST_CHECK_EQUAL(
		NewBotAI_GetDrainlockForceChoice( 0, 1, 0, 90, 60, 40 ),
		NEWBOTAI_DRAINLOCK_FORCE_DRAIN );
	BOOST_CHECK_EQUAL(
		NewBotAI_GetDrainlockForceChoice( 0, 0, 1, 90, 60, 40 ),
		NEWBOTAI_DRAINLOCK_FORCE_NONE );
}

BOOST_AUTO_TEST_CASE( accidental_special_guard_uses_effective_inputs )
{
	BOOST_CHECK( NewBotAI_ShouldBlockOrthogonalSaberSpecialInput( 1, -1, 0, 1, 0, 1 ) );
	BOOST_CHECK_EQUAL( NewBotAI_GetEffectiveMoveInput( 0, -1 ), -1 );
	BOOST_CHECK( !NewBotAI_ShouldBlockOrthogonalSaberSpecialInput( 1, -1, 1, 1, 0, 1 ) );
	BOOST_CHECK( !NewBotAI_ShouldBlockOrthogonalSaberSpecialInput( 1, 0, 0, 1, 0, 1 ) );
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE_END()
