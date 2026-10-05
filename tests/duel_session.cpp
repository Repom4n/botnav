#include "g_duel_session.h"

#include <boost/test/unit_test.hpp>
#include <cstring>

BOOST_AUTO_TEST_SUITE( duel_session )

BOOST_AUTO_TEST_CASE( nonce_initializes_bundled_sqlite_on_cold_start )
{
	BOOST_REQUIRE_EQUAL(sqlite3_shutdown(), SQLITE_OK);
	unsigned char nonce[16];
	BOOST_REQUIRE_EQUAL(G_GenerateLearningSessionNonce(nonce), SQLITE_OK);
	sqlite3 *db = NULL;
	BOOST_REQUIRE_EQUAL(sqlite3_open(":memory:", &db), SQLITE_OK);
	BOOST_CHECK_EQUAL(sqlite3_exec(db, "SELECT 1", NULL, NULL, NULL), SQLITE_OK);
	BOOST_CHECK_EQUAL(sqlite3_close(db), SQLITE_OK);
	BOOST_CHECK_EQUAL(sqlite3_shutdown(), SQLITE_OK);
}

BOOST_AUTO_TEST_CASE( repeated_map_sessions_keep_sqlite_initialized )
{
	BOOST_REQUIRE_EQUAL(sqlite3_shutdown(), SQLITE_OK);
	unsigned char nonce[16];
	for (int map = 0; map < 3; ++map)
		BOOST_REQUIRE_EQUAL(G_GenerateLearningSessionNonce(nonce), SQLITE_OK);
	BOOST_CHECK_EQUAL(sqlite3_shutdown(), SQLITE_OK);
}

static int FailSQLiteAllocatorInitialization(void *)
{
	return SQLITE_NOMEM;
}

BOOST_AUTO_TEST_CASE( initialization_failure_does_not_enter_prng )
{
	BOOST_REQUIRE_EQUAL(sqlite3_shutdown(), SQLITE_OK);
	sqlite3_mem_methods original;
	BOOST_REQUIRE_EQUAL(sqlite3_config(SQLITE_CONFIG_GETMALLOC, &original), SQLITE_OK);
	sqlite3_mem_methods failing = original;
	failing.xInit = FailSQLiteAllocatorInitialization;
	BOOST_REQUIRE_EQUAL(sqlite3_config(SQLITE_CONFIG_MALLOC, &failing), SQLITE_OK);
	unsigned char nonce[16];
	std::memset(nonce, 0xA5, sizeof(nonce));
	const int result = G_GenerateLearningSessionNonce(nonce);
	BOOST_REQUIRE_EQUAL(sqlite3_shutdown(), SQLITE_OK);
	BOOST_REQUIRE_EQUAL(sqlite3_config(SQLITE_CONFIG_MALLOC, &original), SQLITE_OK);
	BOOST_CHECK_EQUAL(result, SQLITE_NOMEM);
	for (unsigned char byte : nonce)
		BOOST_CHECK_EQUAL(byte, 0xA5);
}

BOOST_AUTO_TEST_SUITE_END()
