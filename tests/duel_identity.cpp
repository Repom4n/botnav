#include "g_duel_identity.h"

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE( duel_identity )

BOOST_AUTO_TEST_CASE( bot_ladder_key_uses_bot_file_and_level )
{
	char key[32];

	BOOST_CHECK( G_FormatBotDuelIdentity( "botfiles/mediumds.jkb", "Kyle", 5, key, sizeof( key ) ) );
	BOOST_CHECK_EQUAL( key, "mediumds [bot L5]" );

	BOOST_CHECK( G_FormatBotDuelIdentity( "botfiles\\hardls.jkb", "Luke", 10, key, sizeof( key ) ) );
	BOOST_CHECK_EQUAL( key, "hardls [bot L10]" );

	BOOST_CHECK( G_FormatBotDuelIdentity( "", "Kyle", 3, key, sizeof( key ) ) );
	BOOST_CHECK_EQUAL( key, "Kyle [bot L3]" );

	BOOST_CHECK( !G_FormatBotDuelIdentity( NULL, NULL, 3, key, sizeof( key ) ) );
	BOOST_CHECK_EQUAL( key, "" );
}

BOOST_AUTO_TEST_CASE( bot_key_with_two_digit_level_fits_32_bytes )
{
	char key[32];

	G_FormatBotDuelIdentity( "botfiles/mediumds.jkb", NULL, 10, key, sizeof( key ) );
	BOOST_CHECK_EQUAL( key, "mediumds [bot L10]" );
	BOOST_CHECK( strlen( key ) < sizeof( key ) );
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
	BOOST_CHECK( !G_IsGuestDuelKey( "mediumds [bot L5]" ) );
	BOOST_CHECK( !G_IsGuestDuelKey( NULL ) );
}

BOOST_AUTO_TEST_SUITE_END()
