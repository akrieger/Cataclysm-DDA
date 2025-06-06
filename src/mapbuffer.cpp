#include "mapbuffer.h"

#include <chrono>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <functional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "cata_path.h"
#include "cata_utility.h"
#include "debug.h"
#include "filesystem.h"
#include "flexbuffer_json.h"
#include "input.h"
#include "json.h"
#include "json_loader.h"
#include "map.h"
#include "output.h"
#include "overmapbuffer.h"
#include "path_info.h"
#include "point.h"
#include "popup.h"
#include "std_hash_fs_path.h"
#include "string_formatter.h"
#include "submap.h"
#include "translations.h"
#include "type_id.h"
#include "ui_manager.h"
#include "worldfactory.h"
#include "zzip.h"

#define dbg(x) DebugLog((x),D_MAP) << __FILE__ << ":" << __LINE__ << ": "

class game;

// NOLINTNEXTLINE(cata-static-declarations)
extern std::unique_ptr<game> g;
// NOLINTNEXTLINE(cata-static-declarations)
extern const int savegame_version;

static std::string quad_file_name( const tripoint_abs_omt &om_addr )
{
    return string_format( "%d.%d.%d.map", om_addr.x(), om_addr.y(), om_addr.z() );
}

static cata_path find_dirname( const tripoint_abs_omt &om_addr )
{
    const tripoint_abs_seg segment_addr = project_to<coords::seg>( om_addr );
    return PATH_INFO::world_base_save_path() / "maps" / string_format( "%d.%d.%d",
            segment_addr.x(),
            segment_addr.y(), segment_addr.z() );
}

mapbuffer MAPBUFFER;

mapbuffer::mapbuffer() = default;
mapbuffer::~mapbuffer() = default;

void mapbuffer::clear()
{
    submaps.clear();
}

void mapbuffer::clear_outside_reality_bubble()
{
    map &here = get_map();
    auto it = submaps.begin();
    while( it != submaps.end() ) {
        if( here.inbounds( it->first ) ) {
            ++it;
        } else {
            it = submaps.erase( it );
        }
    }
}

bool mapbuffer::add_submap( const tripoint_abs_sm &p, std::unique_ptr<submap> &sm )
{
    if( submaps.count( p ) ) {
        return false;
    }

    submaps[p] = std::move( sm );

    return true;
}

bool mapbuffer::add_submap( const tripoint_abs_sm &p, submap *sm )
{
    // FIXME: get rid of this overload and make submap ownership semantics sane.
    std::unique_ptr<submap> temp( sm );
    bool result = add_submap( p, temp );
    if( !result ) {
        // NOLINTNEXTLINE( bugprone-unused-return-value )
        temp.release();
    }
    return result;
}

void mapbuffer::remove_submap( const tripoint_abs_sm &addr )
{
    auto m_target = submaps.find( addr );
    if( m_target == submaps.end() ) {
        debugmsg( "Tried to remove non-existing submap %s", addr.to_string() );
        return;
    }
    submaps.erase( m_target );
}

submap *mapbuffer::lookup_submap( const tripoint_abs_sm &p )
{
    dbg( D_INFO ) << "mapbuffer::lookup_submap( x[" << p.x() << "], y[" << p.y() << "], z["
                  << p.z() << "])";

    const auto iter = submaps.find( p );
    if( iter == submaps.end() ) {
        try {
            return unserialize_submaps( p );
        } catch( const std::exception &err ) {
            debugmsg( "Failed to load submap %s: %s", p.to_string(), err.what() );
        }
        return nullptr;
    }

    return iter->second.get();
}

bool mapbuffer::submap_exists( const tripoint_abs_sm &p )
{
    // Could so with a second check against a std::unordered_set<tripoint_abs_sm> of already checked existing but not loaded submaps before resorting to unserializing?
    const auto iter = submaps.find( p );
    if( iter == submaps.end() ) {
        try {
            return unserialize_submaps( p );
        } catch( const std::exception &err ) {
            debugmsg( "Failed to load submap %s: %s", p.to_string(), err.what() );
        }
        return false;
    }

    return true;
}

bool mapbuffer::submap_exists_approx( const tripoint_abs_sm &p )
{
    const auto iter = submaps.find( p );
    if( iter == submaps.end() ) {
        try {
            const tripoint_abs_omt om_addr = project_to<coords::omt>( p );
            const cata_path dirname = find_dirname( om_addr );
            std::string file_name = quad_file_name( om_addr );

            if( world_generator->active_world->has_compression_enabled() ) {
                cata_path zzip_name = dirname;
                zzip_name += ".zzip";
                if( !file_exist( zzip_name ) ) {
                    return false;
                }
                std::shared_ptr<zzip> z = zzip::load( zzip_name.get_unrelative_path(),
                                                      ( PATH_INFO::world_base_save_path() / "maps.dict" ).get_unrelative_path() );
                return z->has_file( std::filesystem::u8path( file_name ) );
            } else {
                return file_exist( dirname / file_name );
            }
        } catch( const std::exception &err ) {
            debugmsg( "Failed to load submap %s: %s", p.to_string(), err.what() );
        }
        return false;
    }

    return true;
}

void mapbuffer::save( bool delete_after_save )
{
    assure_dir_exist( PATH_INFO::world_base_save_path() / "maps" );

    int num_saved_submaps = 0;
    int num_total_submaps = submaps.size();

    map &here = get_map();

    static_popup popup;

    // A set of already-saved submaps, in global overmap coordinates.
    std::set<tripoint_abs_omt> saved_submaps;
    std::list<tripoint_abs_sm> submaps_to_delete;
    static constexpr std::chrono::milliseconds update_interval( 500 );
    std::chrono::steady_clock::time_point last_update = std::chrono::steady_clock::now();

    for( auto &elem : submaps ) {
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        if( last_update + update_interval < now ) {
            popup.message( _( "Please wait as the map saves [%d/%d]" ),
                           num_saved_submaps, num_total_submaps );
            ui_manager::redraw();
            refresh_display();
            inp_mngr.pump_events();
            last_update = now;
        }
        // Whatever the coordinates of the current submap are,
        // we're saving a 2x2 quad of submaps at a time.
        // Submaps are generated in quads, so we know if we have one member of a quad,
        // we have the rest of it, if that assumption is broken we have REAL problems.
        const tripoint_abs_omt om_addr = project_to<coords::omt>( elem.first );
        if( saved_submaps.count( om_addr ) != 0 ) {
            // Already handled this one.
            continue;
        }
        saved_submaps.insert( om_addr );

        // A segment is a chunk of 32x32 submap quads.
        // We're breaking them into subdirectories so there aren't too many files per directory.
        // Might want to make a set for this one too so it's only checked once per save().
        const cata_path dirname = find_dirname( om_addr );
        const cata_path quad_path = dirname / quad_file_name( om_addr );

        bool inside_reality_bubble = here.inbounds( om_addr );
        // delete_on_save deletes everything, otherwise delete submaps
        // outside the current map.
        save_quad( dirname, quad_path, om_addr, submaps_to_delete,
                   delete_after_save || !inside_reality_bubble );
        num_saved_submaps += 4;
    }
    for( auto &elem : submaps_to_delete ) {
        remove_submap( elem );
    }
}

void mapbuffer::save_quad(
    const cata_path &dirname, const cata_path &filename, const tripoint_abs_omt &om_addr,
    std::list<tripoint_abs_sm> &submaps_to_delete, bool delete_after_save )
{
    std::vector<point_rel_sm> offsets;
    std::vector<tripoint_abs_sm> submap_addrs;
    offsets.reserve( 4 );
    submap_addrs.reserve( 4 );
    offsets.push_back( point_rel_sm::zero );
    offsets.push_back( point_rel_sm::south );
    offsets.push_back( point_rel_sm::east );
    offsets.push_back( point_rel_sm::south_east );

    bool all_uniform = true;
    bool reverted_to_uniform = false;
    bool file_exists = false;

    std::shared_ptr<zzip> z;
    // The number of uniform submaps is so enormous that the filesystem overhead
    // for this step of just checking if the quad exists approaches 70% of the
    // total cost of saving the mapbuffer, in one test save I had.
    if( world_generator->active_world->has_compression_enabled() ) {
        cata_path zzip_name = dirname;
        zzip_name += ".zzip";
        z = zzip::load( zzip_name.get_unrelative_path(),
                        ( PATH_INFO::world_base_save_path() / "maps.dict" ).get_unrelative_path() );
        if( !z ) {
            throw std::runtime_error( "Failed opening compressed save file " +
                                      zzip_name.get_unrelative_path().generic_u8string() );
        }
        file_exists = z->has_file( filename.get_relative_path().filename() );
    } else {
        file_exists = std::filesystem::exists( filename.get_unrelative_path() );
    }

    for( point_rel_sm &offsets_offset : offsets ) {
        tripoint_abs_sm submap_addr = project_to<coords::sm>( om_addr );
        submap_addr += offsets_offset.raw(); // TODO: Make += etc. available to relative parameters as well.
        submap_addrs.push_back( submap_addr );
        submap *sm = submaps[submap_addr].get();
        if( sm != nullptr ) {
            if( !sm->is_uniform() ) {
                all_uniform = false;
            } else if( sm->reverted ) {
                reverted_to_uniform = file_exists;
            }
        }
    }

    if( all_uniform ) {
        // Nothing to save - this quad will be regenerated faster than it would be re-read
        if( delete_after_save ) {
            for( auto &submap_addr : submap_addrs ) {
                if( submaps.count( submap_addr ) > 0 && submaps[submap_addr] != nullptr ) {
                    submaps_to_delete.push_back( submap_addr );
                }
            }
        }

        // deleting the file might fail on some platforms in some edge cases so force serialize this
        // uniform quad
        if( !reverted_to_uniform ) {
            return;
        }
    }

    std::stringstream stringout;
    JsonOut jsout( stringout );
    jsout.start_array();
    for( auto &submap_addr : submap_addrs ) {
        if( submaps.count( submap_addr ) == 0 ) {
            continue;
        }

        submap *sm = submaps[submap_addr].get();

        if( sm == nullptr ) {
            continue;
        }

        jsout.start_object();

        jsout.member( "version", savegame_version );
        jsout.member( "coordinates" );

        jsout.start_array();
        jsout.write( submap_addr.x() );
        jsout.write( submap_addr.y() );
        jsout.write( submap_addr.z() );
        jsout.end_array();

        sm->store( jsout );

        jsout.end_object();

        if( delete_after_save ) {
            submaps_to_delete.push_back( submap_addr );
        }
    }

    jsout.end_array();

    std::string s = std::move( stringout ).str();

    if( z ) {
        z->add_file( filename.get_relative_path().filename(), s );
        z->compact( 2.0 );
    } else {
        // Don't create the directory if it would be empty
        assure_dir_exist( dirname );
        write_to_file( filename, [&]( std::ostream & fout ) {
            fout << s;
        } );
    }

    if( all_uniform && reverted_to_uniform ) {
        if( z ) {
            z->delete_files( { filename.get_relative_path().filename() } );
        } else {
            std::filesystem::remove( filename.get_unrelative_path() );
        }
    }
}

// We're reading in way too many entities here to mess around with creating sub-objects and
// seeking around in them, so we're using the json streaming API.
submap *mapbuffer::unserialize_submaps( const tripoint_abs_sm &p )
{
    // Map the tripoint to the submap quad that stores it.
    const tripoint_abs_omt om_addr = project_to<coords::omt>( p );
    const cata_path dirname = find_dirname( om_addr );
    std::string file_name = quad_file_name( om_addr );
    std::filesystem::path file_name_path = std::filesystem::u8path( file_name );
    cata_path quad_path = dirname / file_name;

    bool read = [&] {
        if( world_generator->active_world->has_compression_enabled() )
        {
            cata_path zzip_name = dirname;
            zzip_name += ".zzip";
            if( !file_exist( zzip_name ) ) {
                return false;
            }
            std::shared_ptr<zzip> z = zzip::load( zzip_name.get_unrelative_path(),
                                                  ( PATH_INFO::world_base_save_path() / "maps.dict" ).get_unrelative_path() );
            if( !z->has_file( file_name_path ) ) {
                return false;
            }
            std::vector<std::byte> contents = z->get_file( file_name_path );
            std::string_view string_contents{ reinterpret_cast<char *>( contents.data() ), contents.size() };
            JsonValue jsin = json_loader::from_string( std::string( string_contents ) );
            try {
                deserialize( jsin );
            } catch( std::exception &err ) {
                debugmsg( _( "Failed to read from \"%1$s\": %2$s" ), zzip_name.generic_u8string() + ":" + file_name,
                          err.what() );
                return false;
            }
            return true;
        } else
        {
            return read_from_file_optional_json( quad_path, [this]( const JsonValue & jsin ) {
                deserialize( jsin );
            } );

        }
    }();

    if( !read ) {
        return nullptr;
    }

    // fill in uniform submaps that were not serialized. Note that failure as a result of it
    // not being uniform is OK and results in any missing uniform submaps being generated.
    oter_id const oid = overmap_buffer.ter( om_addr );
    generate_uniform_omt( project_to<coords::sm>( om_addr ), oid );
    if( submaps.count( p ) == 0 ) {
        debugmsg( "file %s did not contain the expected submap %s for non-uniform terrain %s",
                  quad_path.generic_u8string(), p.to_string(), oid.id().str() );
    }

    return submaps[ p ].get();
}

void mapbuffer::deserialize( const JsonArray &ja )
{
    for( JsonObject submap_json : ja ) {
        std::unique_ptr<submap> sm = std::make_unique<submap>();
        tripoint_abs_sm submap_coordinates;
        int version = 0;
        // We have to read version first because the iteration order of json members is undefined.
        if( submap_json.has_int( "version" ) ) {
            version = submap_json.get_int( "version" );
        }
        for( JsonMember submap_member : submap_json ) {
            std::string submap_member_name = submap_member.name();
            if( submap_member_name == "coordinates" ) {
                JsonArray coords_array = submap_member;
                tripoint_abs_sm loc{ coords_array.next_int(), coords_array.next_int(), coords_array.next_int() };
                submap_coordinates = loc;
            } else {
                sm->load( submap_member, submap_member_name, version );
            }
        }

        if( !add_submap( submap_coordinates, sm ) ) {
            debugmsg( "submap %s was already loaded", submap_coordinates.to_string() );
        }
    }
}
