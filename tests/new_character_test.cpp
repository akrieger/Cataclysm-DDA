#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "avatar.h"
#include "cata_catch.h"
#include "inventory.h"
#include "item.h"
#include "mutation.h"
#include "pimpl.h"
#include "player_helpers.h"
#include "profession.h"
#include "scenario.h"
#include "string_formatter.h"
#include "type_id.h"
#include "visitable.h"

static const trait_id trait_ALBINO( "ALBINO" );
static const trait_id trait_ANTIFRUIT( "ANTIFRUIT" );
static const trait_id trait_ANTIJUNK( "ANTIJUNK" );
static const trait_id trait_ANTIWHEAT( "ANTIWHEAT" );
static const trait_id trait_ASTHMA( "ASTHMA" );
static const trait_id trait_CANNIBAL( "CANNIBAL" );
static const trait_id trait_LACTOSE( "LACTOSE" );
static const trait_id trait_MEATARIAN( "MEATARIAN" );
static const trait_id trait_TAIL_FLUFFY( "TAIL_FLUFFY" );
static const trait_id trait_VEGETARIAN( "VEGETARIAN" );
static const trait_id trait_WOOLALLERGY( "WOOLALLERGY" );

static std::ostream &operator<<( std::ostream &s, const std::vector<trait_id> &v )
{
    for( const auto &e : v ) {
        s << e.c_str() << " ";
    }
    return s;
}

static std::vector<trait_id> next_subset( const std::vector<trait_id> &set )
{
    // Doing it this way conveniently returns a vector containing solely set[foo] before
    // it returns any other vectors with set[foo] in it
    static unsigned bitset = 0;
    std::vector<trait_id> ret;

    ++bitset;
    // Check each bit position for a match
    for( size_t idx = 0; idx < set.size(); idx++ ) {
        if( bitset & ( 1 << idx ) ) {
            ret.push_back( set[idx] );
        }
    }
    return ret;
}

static bool try_set_traits( const std::vector<trait_id> &traits )
{
    avatar &player_character = get_avatar();
    player_character.clear_mutations();
    player_character.add_traits(); // mandatory prof/scen traits
    std::vector<trait_id> oked_traits;
    for( const trait_id &tr : traits ) {
        if( player_character.has_conflicting_trait( tr ) ||
            !get_scenario()->traitquery( tr ) ) {
            return false;
        } else if( !player_character.has_trait( tr ) ) {
            oked_traits.push_back( tr );
        }
    }
    player_character.set_mutations( oked_traits );
    return true;
}

static int get_item_count( const std::set<const item *> &items )
{
    int sum = 0;
    for( const item *it : items ) {
        sum += it->count();
    }
    return sum;
}

struct failure {
    string_id<profession> prof;
    std::vector<trait_id> mut;
    itype_id item_name;
    std::string reason;
};

namespace std
{
template<>
struct less<failure> {
    bool operator()( const failure &lhs, const failure &rhs ) const {
        return lhs.prof < rhs.prof;
    }
};
} // namespace std

// TODO: According to profiling (interrupt, backtrace, wait a few seconds, repeat) with a sample
// size of 20, 70% of the time is due to the call to Character::set_mutation in try_set_traits.
// When the mutation stuff isn't commented out, the test takes 110 minutes (not a typo)!

/**
 * Disabled temporarily because 3169 profession combinations do not work and need to be fixed in json
 */
TEST_CASE( "starting_items", "[slow]" )
{
}

TEST_CASE( "Generated_character_with_category_mutations", "[mutation]" )
{
    REQUIRE( !trait_TAIL_FLUFFY.obj().category.empty() );
    avatar &u = get_avatar();
    clear_avatar();
    REQUIRE( u.get_mutations().empty() );
    REQUIRE( u.get_base_traits().empty() );
    REQUIRE( u.mutation_category_level.empty() );

    SECTION( "Mutations have category levels" ) {
        u.toggle_trait_deps( trait_TAIL_FLUFFY );
        CHECK( u.has_trait( trait_TAIL_FLUFFY ) );
        CHECK( !u.get_mutations().empty() );
        CHECK( u.get_base_traits().empty() );
        CHECK( !u.mutation_category_level.empty() );
        u.toggle_trait_deps( trait_TAIL_FLUFFY );
        CHECK( !u.has_trait( trait_TAIL_FLUFFY ) );
        CHECK( u.get_mutations().empty() );
        CHECK( u.get_base_traits().empty() );
        CHECK( u.mutation_category_level.empty() );
    }

    SECTION( "Category mutations can be removed" ) {
        u.toggle_trait_deps( trait_TAIL_FLUFFY );
        CHECK( u.has_trait( trait_TAIL_FLUFFY ) );
        u.remove_mutation( trait_TAIL_FLUFFY );
        CHECK( !u.has_trait( trait_TAIL_FLUFFY ) );
    }
}

TEST_CASE( "cannibal_not_randomly_selected", " [character] [traits] [random]" )
{
    for( int i = 0; i < 1000; ++i ) {
        trait_id random_trait = get_avatar().random_bad_trait();
        REQUIRE( random_trait != trait_CANNIBAL );
    }
}
