#include "flexbuffer_json.h"

#include <fstream>

#include "json.h"

// Advances the given jsin according to the passed JsonPath
static void advance_jsin( JsonIn *jsin, flexbuffers::Reference root, FlexJsonPath const &path )
{
    for( auto idx : path ) {
        if( root.IsMap() ) {
            std::string member = root.AsMap().Keys()[ idx ].AsString().str();
            jsin->start_object();
            while( !jsin->end_object() ) {
                if( jsin->get_member_name() == member ) {
                    break;
                }
                jsin->skip_value();
            }
        } else {
            for( size_t i = 0; i < idx; ++i ) {
                jsin->skip_value();
            }
        }
        root = root.AsVector()[ idx ];
    }
}

void FlexJson::throw_error( std::string const &message ) const
{
    std::ifstream original_json( source_file_, std::ifstream::in | std::ifstream::binary );
    JsonIn jsin( original_json );

    advance_jsin( &jsin, /*root_*/, path_ );

    jsin.error( message );
}

std::string FlexJson::str() const
{
    std::string ret;
    json_.ToString( false, true, ret );
    return ret;
}

void FlexJsonObject::error_no_member( std::string const &member ) const
{
    std::ifstream original_json( source_file_, std::ifstream::in | std::ifstream::binary );
    JsonIn jsin( original_json );

    advance_jsin( &jsin, /*root_*/, path_ );

    JsonObject jo = jsin.get_object();
    jo.allow_omitted_members();
    jo.get_member( member );
    // Just to make sure the compiler understands we will error earlier.
    jo.throw_error( "Failed to report missing member " + member );
}

void FlexJsonObject::error_skipped_members( std::vector<size_t> const &skipped_members ) const
{
    std::ifstream original_json( source_file_, std::ifstream::in | std::ifstream::binary );
    JsonIn jsin( original_json );

    advance_jsin( &jsin, /*root_*/, path_ );

    JsonObject jo = jsin.get_object();
    jo.allow_omitted_members();
    for( size_t skipped_member_idx : skipped_members ) {
        flexbuffers::String name = keys_[ skipped_member_idx ].AsString();
        try {
            jo.throw_error( string_format( "Invalid or misplaced field name \"%s\" in JSON data",
                                           name.c_str() ), name.c_str() );
        } catch( const JsonError &e ) {
            debugmsg( "(json-error)\n%s", e.what() );
        }
        mark_visited( skipped_member_idx );
    }
}

json_source_location FlexJsonObject::get_source_location() const
{
    if( source_file_.empty() ) {
        throw_error( "JsonObject::get_source_location called but the path is unknown" );
    }

    std::ifstream original_json( source_file_, std::ifstream::in | std::ifstream::binary );
    JsonIn jsin( original_json );

    advance_jsin( &jsin, /*root_*/, path_ );

    json_source_location loc;
    loc.path = make_shared_fast<std::string>( source_file_ );
    loc.offset = jsin.tell();
    return loc;
}
