#include "g_duel_identity.h"

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE( duel_identity )

BOOST_AUTO_TEST_CASE( bot_ladder_key_is_one_identity_per_level )
{
	char key[32];

	BOOST_CHECK( G_FormatBotDuelIdentity( "botfiles/mediumds.jkb", "Kyle", 5, key, sizeof( key ) ) );
	BOOST_CHECK_EQUAL( key, "botlvl5" );

	BOOST_CHECK( G_FormatBotDuelIdentity( "botfiles\\hardls.jkb", "Luke", 10, key, sizeof( key ) ) );
	BOOST_CHECK_EQUAL( key, "botlvl10" );

	BOOST_CHECK( G_FormatBotDuelIdentity( "", "Kyle", 3, key, sizeof( key ) ) );
	BOOST_CHECK_EQUAL( key, "botlvl3" );

	BOOST_CHECK( G_FormatBotDuelIdentity( NULL, NULL, 3, key, sizeof( key ) ) );
	BOOST_CHECK_EQUAL( key, "botlvl3" );
}

BOOST_AUTO_TEST_CASE( bot_key_without_level_falls_back_to_bot_file )
{
	char key[32];

	BOOST_CHECK( G_FormatBotDuelIdentity( "botfiles/mediumds.jkb", "Kyle", 0, key, sizeof( key ) ) );
	BOOST_CHECK_EQUAL( key, "mediumds" );

	BOOST_CHECK( !G_FormatBotDuelIdentity( NULL, NULL, 0, key, sizeof( key ) ) );
	BOOST_CHECK_EQUAL( key, "" );
}

BOOST_AUTO_TEST_CASE( guests_are_rated_only_against_bots )
{
	BOOST_CHECK( G_ShouldUseGuestDuelKey( 0, 0, 1, 1 ) );
	BOOST_CHECK( !G_ShouldUseGuestDuelKey( 0, 0, 0, 0 ) );
	BOOST_CHECK( !G_ShouldUseGuestDuelKey( 0, 0, 0, 1 ) );
	BOOST_CHECK( !G_ShouldUseGuestDuelKey( 1, 0, 1, 1 ) );
	BOOST_CHECK( !G_ShouldUseGuestDuelKey( 0, 1, 1, 1 ) );
	BOOST_CHECK( !G_ShouldUseGuestDuelKey( 0, 0, 1, 0 ) );

	BOOST_CHECK( G_IsGuestDuelKey( "iphash:0123abcd" ) );
	BOOST_CHECK( !G_IsGuestDuelKey( "botlvl5" ) );
	BOOST_CHECK( !G_IsGuestDuelKey( NULL ) );
}

BOOST_AUTO_TEST_SUITE_END()
