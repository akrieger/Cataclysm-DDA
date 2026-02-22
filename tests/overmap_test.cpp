#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "calendar.h"
#include "cata_catch.h"
#include "city.h"
#include "common_types.h"
#include "coordinates.h"
#include "debug.h"
#include "enum_conversions.h"
#include "enums.h"
#include "game.h"
#include "global_vars.h"
#include "item.h"
#include "item_factory.h"
#include "itype.h"
#include "map.h"
#include "map_helpers.h"
#include "map_iterator.h"
#include "map_scale_constants.h"
#include "mapbuffer.h"
#include "omdata.h"
#include "options.h"
#include "output.h"
#include "overmap.h"
#include "overmap_location.h"
#include "overmap_types.h"
#include "overmapbuffer.h"
#include "point.h"
#include "recipe.h"
#include "rng.h"
#include "test_data.h"
#include "type_id.h"
#include "value_ptr.h"
#include "vehicle.h"
#include "vpart_position.h"

static const oter_str_id oter_cabin( "cabin" );
static const oter_str_id oter_cabin_east( "cabin_east" );
static const oter_str_id oter_cabin_north( "cabin_north" );
static const oter_str_id oter_cabin_south( "cabin_south" );
static const oter_str_id oter_cabin_west( "cabin_west" );

static const overmap_special_id overmap_special_Cabin( "Cabin" );
static const overmap_special_id overmap_special_Lab( "Lab" );

static std::vector<oter_flags> all_oter_flags()
{
    const int max_flag = static_cast<int>( oter_flags::num_oter_flags );
    std::vector<oter_flags> flags;
    flags.reserve( max_flag );
    for( int i = 0; i < max_flag; ++i ) {
        flags.emplace_back( static_cast<oter_flags>( i ) );
    }
    return flags;
}

static std::vector<std::string> location_flag_strings()
{
    std::unordered_set<std::string> unique_flags;
    for( const overmap_location &loc : overmap_locations::get_all() ) {
        for( const std::string &flag : loc.get_flags() ) {
            unique_flags.insert( flag );
        }
    }
    std::vector<std::string> result( unique_flags.begin(), unique_flags.end() );
    std::sort( result.begin(), result.end() );
    return result;
}

static bool any_terrain_with_flag( const oter_flags flag )
{
    for( const oter_t &ter : overmap_terrains::get_all() ) {
        if( ter.has_flag( flag ) ) {
            return true;
        }
    }
    return false;
}

TEST_CASE( "oter_flags_string_round_trip", "[overmap][flags]" )
{
    const auto &flag_map = io::get_enum_lookup_map<oter_flags>();
    const std::vector<oter_flags> flags = all_oter_flags();

    CHECK( flag_map.size() == flags.size() );

    std::unordered_set<std::string> seen_strings;
    for( const oter_flags flag : flags ) {
        const std::string flag_string = io::enum_to_string( flag );
        CAPTURE( flag_string );
        CHECK_FALSE( flag_string.empty() );
        CHECK( flag_map.count( flag_string ) == 1 );
        CHECK( io::string_to_enum<oter_flags>( flag_string ) == flag );
        CHECK( seen_strings.emplace( flag_string ).second );
    }
}

TEST_CASE( "overmap_location_flags_are_valid", "[overmap][flags]" )
{
    const std::vector<std::string> flags = location_flag_strings();

    for( const std::string &flag : flags ) {
        CAPTURE( flag );
        CHECK( io::enum_is_valid<oter_flags>( flag ) );
        CHECK( io::string_to_enum_optional<oter_flags>( flag ).has_value() );
    }
}

TEST_CASE( "overmap_location_flags_match_terrain_flags", "[overmap][flags]" )
{
    const std::vector<std::string> flags = location_flag_strings();
    const auto &flag_map = io::get_enum_lookup_map<oter_flags>();

    for( const std::string &flag : flags ) {
        const auto iter = flag_map.find( flag );
        if( iter == flag_map.end() ) {
            continue;
        }
        CAPTURE( flag );
        CHECK( any_terrain_with_flag( iter->second ) );
    }
}

TEST_CASE( "set_and_get_overmap_scents", "[overmap]" )
{
    std::unique_ptr<overmap> test_overmap = std::make_unique<overmap>( point_abs_om() );

    // By default there are no scents set.
    for( int x = 0; x < 180; ++x ) {
        for( int y = 0; y < 180; ++y ) {
            for( int z = -10; z < 10; ++z ) {
                REQUIRE( test_overmap->scent_at( { x, y, z } ).creation_time ==
                         calendar::before_time_starts );
            }
        }
    }

    time_point creation_time = calendar::turn_zero + 50_turns;
    scent_trace test_scent( creation_time, 90 );
    test_overmap->set_scent( { 75, 85, 0 }, test_scent );
    REQUIRE( test_overmap->scent_at( { 75, 85, 0} ).creation_time == creation_time );
    REQUIRE( test_overmap->scent_at( { 75, 85, 0} ).initial_strength == 90 );
}

TEST_CASE( "default_overmap_generation_always_succeeds", "[overmap][slow]" )
{
    overmap_buffer.clear();
    int overmaps_to_construct = 10;
    for( const point_abs_om &candidate_addr : closest_points_first( point_abs_om(), 10 ) ) {
        // Skip populated overmaps.
        if( overmap_buffer.has( candidate_addr ) ) {
            continue;
        }
        overmap_special_batch test_specials = overmap_specials::get_default_batch( candidate_addr );
        overmap_buffer.create_custom_overmap( candidate_addr, test_specials );
        for( const overmap_special_placement &special_placement : test_specials ) {
            const overmap_special *special = special_placement.special_details;
            INFO( "In attempt #" << overmaps_to_construct
                  << " failed to place " << special->id.str() );
            int min_occur = special->get_constraints().occurrences.min;
            CHECK( min_occur <= special_placement.instances_placed );
        }
        if( --overmaps_to_construct <= 0 ) {
            break;
        }
    }
}

TEST_CASE( "default_overmap_generation_has_non_mandatory_specials_at_origin", "[overmap][slow]" )
{
    const point_abs_om origin{};

    overmap_special mandatory;
    overmap_special optional;

    overmap_buffer.clear();
    // Get some specific overmap specials so we can assert their presence later.
    // This should probably be replaced with some custom specials created in
    // memory rather than tying this test to these, but it works for now...
    for( const overmap_special &elem : overmap_specials::get_all() ) {
        if( elem.id == overmap_special_Cabin ) {
            optional = elem;
        } else if( elem.id == overmap_special_Lab ) {
            mandatory = elem;
        }
    }

    // Make this mandatory special impossible to place.
    const_cast<int &>( mandatory.get_constraints().city_size.min ) = 999;

    // Construct our own overmap_special_batch containing only our single mandatory
    // and single optional special, so we can make some assertions.
    std::vector<const overmap_special *> specials;
    specials.push_back( &mandatory );
    specials.push_back( &optional );
    overmap_special_batch test_specials = overmap_special_batch( origin, specials );

    // Run the overmap creation, which will try to place our specials.
    overmap_buffer.create_custom_overmap( origin, test_specials );

    // Get the origin overmap...
    overmap *test_overmap = overmap_buffer.get_existing( origin );

    // ...and assert that the optional special exists on this map.
    bool found_optional = false;
    for( int x = 0; x < OMAPX; ++x ) {
        for( int y = 0; y < OMAPY; ++y ) {
            const oter_id t = test_overmap->ter( { x, y, 0 } );
            if( t->id == oter_cabin ||
                t->id == oter_cabin_north || t->id == oter_cabin_east ||
                t->id == oter_cabin_south || t->id == oter_cabin_west ) {
                found_optional = true;
            }
        }
    }

    INFO( "Failed to place optional special on origin " );
    CHECK( found_optional == true );
}

TEST_CASE( "is_ot_match", "[overmap][terrain]" )
{
    SECTION( "exact match" ) {
        // Matches the complete string
        // NOLINTNEXTLINE(cata-ot-match)
        CHECK( is_ot_match( "forest", oter_id( "forest" ), ot_match_type::exact ) );
        // NOLINTNEXTLINE(cata-ot-match)
        CHECK( is_ot_match( "forest_thick", oter_id( "forest_thick" ), ot_match_type::exact ) );

        // Does not exactly match if rotation differs
        // NOLINTNEXTLINE(cata-ot-match)
        CHECK_FALSE( is_ot_match( "sub_station", oter_id( "sub_station_north" ), ot_match_type::exact ) );
        // NOLINTNEXTLINE(cata-ot-match)
        CHECK_FALSE( is_ot_match( "sub_station", oter_id( "sub_station_south" ), ot_match_type::exact ) );
    }

    SECTION( "type match" ) {
        // Matches regardless of rotation
        // NOLINTNEXTLINE(cata-ot-match)
        CHECK( is_ot_match( "sub_station", oter_id( "sub_station_north" ), ot_match_type::type ) );
        // NOLINTNEXTLINE(cata-ot-match)
        CHECK( is_ot_match( "sub_station", oter_id( "sub_station_south" ), ot_match_type::type ) );
        // NOLINTNEXTLINE(cata-ot-match)
        CHECK( is_ot_match( "sub_station", oter_id( "sub_station_east" ), ot_match_type::type ) );
        // NOLINTNEXTLINE(cata-ot-match)
        CHECK( is_ot_match( "sub_station", oter_id( "sub_station_west" ), ot_match_type::type ) );

        // Does not match if base type does not match
        // NOLINTNEXTLINE(cata-ot-match)
        CHECK_FALSE( is_ot_match( "forest", oter_id( "forest_thick" ), ot_match_type::type ) );
        // NOLINTNEXTLINE(cata-ot-match)
        CHECK_FALSE( is_ot_match( "sub_station", oter_id( "sewer_sub_station" ), ot_match_type::type ) );
    }

    SECTION( "prefix match" ) {
        // Matches the complete string
        CHECK( is_ot_match( "forest", oter_id( "forest" ), ot_match_type::prefix ) );
        CHECK( is_ot_match( "forest_thick", oter_id( "forest_thick" ),
                            ot_match_type::prefix ) );

        // Prefix matches when an underscore separator exists
        CHECK( is_ot_match( "forest", oter_id( "forest_thick" ), ot_match_type::prefix ) );
        CHECK( is_ot_match( "underground", oter_id( "underground_sub_station" ), ot_match_type::prefix ) );

        // Prefix itself may contain underscores
        CHECK( is_ot_match( "sewer_end", oter_id( "sewer_end_north" ), ot_match_type::prefix ) );
        CHECK( is_ot_match( "test_forest_very", oter_id( "test_forest_very_thick" ),
                            ot_match_type::prefix ) );

        // Prefix does not match without an underscore separator
        CHECK_FALSE( is_ot_match( "fore", oter_id( "forest" ), ot_match_type::prefix ) );
        CHECK_FALSE( is_ot_match( "fore", oter_id( "forest_thick" ), ot_match_type::prefix ) );

        // Prefix does not match the middle or end
        CHECK_FALSE( is_ot_match( "sub", oter_id( "sewer_sub_station" ), ot_match_type::prefix ) );
        CHECK_FALSE( is_ot_match( "station", oter_id( "sewer_sub_station" ), ot_match_type::prefix ) );
    }

    SECTION( "contains match" ) {
        // Matches the complete string
        CHECK( is_ot_match( "forest", oter_id( "forest" ), ot_match_type::contains ) );
        CHECK( is_ot_match( "forest_thick", oter_id( "forest_thick" ),
                            ot_match_type::contains ) );

        // Matches the beginning/middle/end of an underscore-delimited id
        CHECK( is_ot_match( "sewer", oter_id( "sewer_sub_station" ), ot_match_type::contains ) );
        CHECK( is_ot_match( "sub", oter_id( "sewer_sub_station" ), ot_match_type::contains ) );
        CHECK( is_ot_match( "station", oter_id( "sewer_sub_station" ), ot_match_type::contains ) );

        // Matches the beginning/middle/end without undercores as well
        CHECK( is_ot_match( "sewe", oter_id( "sewer_sub_station" ), ot_match_type::contains ) );
        CHECK( is_ot_match( "er_su", oter_id( "sewer_sub_station" ), ot_match_type::contains ) );
        CHECK( is_ot_match( "_sub_", oter_id( "sewer_sub_station" ), ot_match_type::contains ) );
        CHECK( is_ot_match( "tion", oter_id( "sewer_sub_station" ), ot_match_type::contains ) );

        // Does not match if substring is not contained
        CHECK_FALSE( is_ot_match( "forest", oter_id( "sewer_sub_station" ), ot_match_type::contains ) );
        CHECK_FALSE( is_ot_match( "forestry", oter_id( "forest" ), ot_match_type::contains ) );
    }
}

TEST_CASE( "mutable_overmap_placement", "[overmap][slow]" )
{
    const overmap_special &special =
        *overmap_special_id( GENERATE( "test_crater", "test_microlab" ) );
    const city cit;

    constexpr int num_overmaps = 100;
    constexpr int num_trials_per_overmap = 100;

    global_variables &globvars = get_globals();
    globvars.clear_global_values();

    for( int j = 0; j < num_overmaps; ++j ) {
        // overmap objects are really large, so we don't want them on the
        // stack.  Use unique_ptr and put it on the heap
        std::unique_ptr<overmap> om = std::make_unique<overmap>( point_abs_om::zero );
        om_direction::type dir = om_direction::type::north;

        int successes = 0;

        for( int i = 0; i < num_trials_per_overmap; ++i ) {
            tripoint_om_omt try_pos( rng( 0, OMAPX - 1 ), rng( 0, OMAPY - 1 ), 0 );

            // This test can get very spammy, so abort once an error is
            // observed
            if( debug_has_error_been_observed() ) {
                return;
            }

            if( om->can_place_special( special, try_pos, dir, false ) ) {
                std::vector<tripoint_om_omt> placed_points =
                    om->place_special( special, try_pos, dir, cit, false, false );
                CHECK( !placed_points.empty() );
                ++successes;
            }
        }

        CAPTURE( special.id.str() );
        CHECK( successes > num_trials_per_overmap / 2 );
    }
}

static bool tally_items( std::unordered_map<itype_id, float> &global_item_count,
                         std::unordered_map<itype_id, int> &item_count, tinymap &tm )
{
    bool found = false;
    for( const tripoint_omt_ms &p : tm.points_on_zlevel() ) {
        for( item &i : tm.i_at( p ) ) {
            std::unordered_map<itype_id, float>::iterator iter = global_item_count.find( i.typeId() );
            if( iter != global_item_count.end() ) {
                found = true;
                item_count.emplace( i.typeId(), 0 ).first->second += i.count();
            }
            for( const item *it : i.all_items_ptr() ) {
                iter = global_item_count.find( it->typeId() );
                if( iter != global_item_count.end() ) {
                    found = true;
                    item_count.emplace( i.typeId(), 0 ).first->second += i.count();
                }
            }
        }
        if( const optional_vpart_position ovp = tm.veh_at( p ) ) {
            vehicle *const veh = &ovp->vehicle();
            for( const int elem : veh->parts_at_relative( ovp->mount_pos(), true ) ) {
                const vehicle_part &vp = veh->part( elem );
                for( item &i : veh->get_items( vp ) ) {
                    std::unordered_map<itype_id, float>::iterator iter = global_item_count.find( i.typeId() );
                    if( iter != global_item_count.end() ) {
                        found = true;
                        item_count.emplace( i.typeId(), 0 ).first->second += i.count();
                    }
                    for( const item *it : i.all_items_ptr() ) {
                        iter = global_item_count.find( it->typeId() );
                        if( iter != global_item_count.end() ) {
                            found = true;
                            item_count.emplace( i.typeId(), 0 ).first->second += i.count();
                        }
                    }
                }
            }
        }
    }
    return found;
}

TEST_CASE( "enumerate_items", "[.]" )
{
    for( const itype *id : item_controller->find(
    []( const itype & type ) -> bool {
    return !!type.gun;
} ) ) {
        printf( "%s ", id->gun->skill_used.str().c_str() );
        printf( "%s\n", id->get_id().str().c_str() );
    }
}

static void finalize_item_counts( std::unordered_map<itype_id, float> &item_counts )
{
    for( std::pair<const std::string, item_demographic_test_data> &category :
         test_data::item_demographics ) {
        // Scan for match ing ammo types and shim them into the provided data so
        // the test doesn't break every time we add a new item variant.
        // Ammo has a lot of things in it we don't consider "real ammo" so there's a list
        // of item types to ignore that are applied here.
        if( category.first == "ammo" ) {
            for( const itype *id : item_controller->find( []( const itype & type ) -> bool {
            return !!type.ammo;
        } ) ) {
                if( category.second.ignored_items.find( id->get_id() ) != category.second.ignored_items.end() ) {
                    continue;
                }
                category.second.item_weights[id->get_id()] = 1;
                auto ammotype_map_iter = category.second.groups.find( id->ammo->type.str() );
                if( ammotype_map_iter == category.second.groups.end() ) {
                    // If there's no matching ammotype in the test data,
                    // stick the item in the "other" group.
                    category.second.groups["other"].second[id->get_id()];
                } else {
                    // If there is a matching ammotype and the item id isn't already populated,
                    // add it.
                    if( ammotype_map_iter->second.second.find( id->get_id() ) ==
                        ammotype_map_iter->second.second.end() ) {
                        ammotype_map_iter->second.second[id->get_id()] = 1;
                    }
                }
            }
        } else if( category.first == "gun" ) {
            for( const itype *id : item_controller->find( []( const itype & type ) -> bool {
            return !!type.gun;
        } ) ) {
                if( category.second.ignored_items.find( id->get_id() ) != category.second.ignored_items.end() ) {
                    continue;
                }
                category.second.item_weights[id->get_id()] = 1; // ???
                for( const ammotype &ammotype_id : id->gun->ammo ) {
                    auto ammotype_map_iter = category.second.groups.find( ammotype_id.str() );
                    if( ammotype_map_iter == category.second.groups.end() ) {
                        // If there's no matching ammotype in the test data,
                        // stick the item in the "other" group.
                        category.second.groups["other"].second[id->get_id()];
                    } else {
                        // If there is a matching ammotype and the item id isn't already populated,
                        // add it.
                        if( ammotype_map_iter->second.second.find( id->get_id() ) ==
                            ammotype_map_iter->second.second.end() ) {
                            ammotype_map_iter->second.second[id->get_id()] = 1; // ???
                        }
                    }
                }
            }
        }
        for( const std::pair<const itype_id, int> &demographics : category.second.item_weights ) {
            item_counts[demographics.first] = 0.0;
        }
    }
}

// Toggle this to enable the (very very very expensive) item demographics test.
static bool enable_item_demographics = false;

TEST_CASE( "overmap_terrain_coverage", "[overmap][slow]" )
{
}

TEST_CASE( "highway_find_intersection_bounds", "[overmap]" )
{
    overmap_buffer.clear();
    highway_intersection_grid &highway_grid =
        overmap_buffer.global_state.highway_intersections;
    highway_grid.set_grid_origin( point_abs_om::zero );
    point_abs_om pos = highway_grid.get_grid_origin();

    const int c_separation = get_option<int>( "HIGHWAY_GRID_COLUMN_SEPARATION" );
    const int r_separation = get_option<int>( "HIGHWAY_GRID_ROW_SEPARATION" );

    const int col_test = c_separation / 2;
    const int row_test = r_separation / 2;
    const int col_test_2 = c_separation * 1.5;
    const int row_test_2 = r_separation * 1.5;

    //check points in 8 directions of origin
    std::vector<std::pair<point_rel_om, point_rel_om>> input_output_pairs = {
        //inside quadrants surrounding origin + 0,0
        { point_rel_om( col_test, 0 ), point_rel_om( 0, 0 ) },
        { point_rel_om( -col_test, 0 ), point_rel_om( -c_separation, 0 ) },
        { point_rel_om( 0, row_test ), point_rel_om( 0, 0 ) },
        { point_rel_om( 0, -row_test ), point_rel_om( 0, -r_separation ) },
        { point_rel_om( col_test, row_test ), point_rel_om( 0, 0 ) },
        { point_rel_om( -col_test, row_test ), point_rel_om( -c_separation, 0 ) },
        { point_rel_om( col_test, -row_test ), point_rel_om( 0, -r_separation ) },
        //on grid points
        { point_rel_om( 0, 0 ), point_rel_om( 0, 0 ) },
        { point_rel_om( -c_separation, 0 ), point_rel_om( -c_separation, 0 ) },
        { point_rel_om( c_separation, 0 ), point_rel_om( c_separation, 0 ) },
        { point_rel_om( 0, -r_separation ), point_rel_om( 0, -r_separation ) },
        { point_rel_om( 0, r_separation ), point_rel_om( 0, r_separation ) },
        //outside quadrants surrounding origin + 0,0
        { point_rel_om( col_test_2, 0 ), point_rel_om( c_separation, 0 ) },
        { point_rel_om( -col_test_2, 0 ), point_rel_om( -c_separation * 2, 0 ) },
        { point_rel_om( 0, row_test_2 ), point_rel_om( 0, r_separation ) },
        { point_rel_om( 0, -row_test_2 ), point_rel_om( 0, -r_separation * 2 ) },
        { point_rel_om( col_test_2, row_test_2 ), point_rel_om( c_separation, r_separation ) },
        { point_rel_om( -col_test_2, row_test_2 ), point_rel_om( -c_separation * 2, r_separation ) },
        { point_rel_om( col_test_2, -row_test_2 ), point_rel_om( c_separation, -r_separation * 2 ) },
        { point_rel_om( -col_test_2, -row_test_2 ), point_rel_om( -c_separation * 2, -r_separation * 2 ) }
    };

    for( const std::pair<point_rel_om, point_rel_om> &p : input_output_pairs ) {
        std::vector<point_abs_om> bounds = highway_grid.find_feature_point_bounds( pos + p.first );
        CHECK( bounds.back() == pos + p.second );
    }

}
