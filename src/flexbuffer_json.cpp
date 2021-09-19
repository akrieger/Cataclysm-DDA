#include "flexbuffer_json.h"

#include <fstream>

#include "json.h"

// Advances the given jsin according to the passed JsonPath
static void advance_jsin( JsonIn *jsin, flexbuffers::Reference root, JsonPath const &path )
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
            jsin->start_array();
            for( size_t i = 0; i < idx && !jsin->end_array(); ++i ) {
                jsin->skip_value();
            }
        }
        root = root.AsVector()[ idx ];
    }
}

void Json::throw_error( JsonPath const &path, std::string const &message, int offset ) const
{
    std::unique_ptr<std::istream> original_json = root_->get_source_stream();
    JsonIn jsin( *original_json );

    advance_jsin( &jsin, flexbuffer_root_from_storage( root_->get_storage() ), path );

    jsin.error( message, offset );
}

void Json::throw_error_after( JsonPath const &path, std::string const &message ) const
{
    std::unique_ptr<std::istream> original_json = root_->get_source_stream();
    JsonIn jsin( *original_json );

    advance_jsin( &jsin, flexbuffer_root_from_storage( root_->get_storage() ), path );
    jsin.skip_value();

    jsin.error( message );
}

void FlexJson::string_error( JsonPath const &path, std::string const &message, int offset ) const
{
    std::unique_ptr<std::istream> original_json = root_->get_source_stream();
    JsonIn jsin( *original_json );

    advance_jsin( &jsin, flexbuffer_root_from_storage( root_->get_storage() ), path );

    jsin.string_error( message, offset );
}

std::string FlexJson::str() const
{
    std::string ret;
    json_.ToString( false, true, ret );
    return ret;
}

FlexJsonObject::~FlexJsonObject()
{
#ifndef CATA_IN_TOOL
#ifdef _DEBUG
    if( !std::uncaught_exception() ) {
        for( size_t idx = 0, end = keys_.size(); idx < end; ++idx ) {
            if( !visited_fields_bitset_.test( idx ) ) {
                const char *name = keys_[idx].AsKey();
                if( strncmp( name, "//", 2 ) != 0 ) {
                    try {
                        throw_error(
                            string_format( "Invalid or misplaced field name \"%s\" in JSON data, or value in unexpected format.",
                                           name ), name );
                    } catch( const JsonError &e ) {
                        debugmsg( "(json-error)\n%s", e.what() );
                    }
                }
            }
        }
    }
#endif
#endif
}

void FlexJsonObject::error_no_member( std::string const &member ) const
{

    std::unique_ptr<std::istream> original_json = root_->get_source_stream();
    JsonIn jsin( *original_json );

    advance_jsin( &jsin, flexbuffer_root_from_storage( root_->get_storage() ), path_ );

    JsonObject jo = jsin.get_object();
    jo.allow_omitted_members();
    jo.get_member( member );
    // Just to make sure the compiler understands we will error earlier.
    jo.throw_error( "Failed to report missing member " + member );
}

void FlexJsonObject::error_skipped_members( std::vector<size_t> const &skipped_members ) const
{
    std::unique_ptr<std::istream> original_json = root_->get_source_stream();
    JsonIn jsin( *original_json );

    advance_jsin( &jsin, flexbuffer_root_from_storage( root_->get_storage() ), path_ );

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

bool FlexJsonValue::read( bool &b, bool throw_on_error ) const
{
    if( !test_bool() ) {
        return error_or_false( throw_on_error, "Expected bool" );
    }
    b = get_bool();
    return true;
}
bool FlexJsonValue::read( char &c, bool throw_on_error ) const
{
    if( !test_number() ) {
        return error_or_false( throw_on_error, "Expected number" );
    }
    c = get_int();
    return true;
}
bool FlexJsonValue::read( signed char &c, bool throw_on_error ) const
{
    if( !test_number() ) {
        return error_or_false( throw_on_error, "Expected number" );
    }
    // TODO: test for overflow
    c = get_int();
    return true;
}
bool FlexJsonValue::read( unsigned char &c, bool throw_on_error ) const
{
    if( !test_number() ) {
        return error_or_false( throw_on_error, "Expected number" );
    }
    // TODO: test for overflow
    c = get_int();
    return true;
}
bool FlexJsonValue::read( short unsigned int &s, bool throw_on_error ) const
{
    if( !test_number() ) {
        return error_or_false( throw_on_error, "Expected number" );
    }
    // TODO: test for overflow
    s = get_int();
    return true;
}
bool FlexJsonValue::read( short int &s, bool throw_on_error ) const
{
    if( !test_number() ) {
        return error_or_false( throw_on_error, "Expected number" );
    }
    // TODO: test for overflow
    s = get_int();
    return true;
}
bool FlexJsonValue::read( int &i, bool throw_on_error ) const
{
    if( !test_number() ) {
        return error_or_false( throw_on_error, "Expected number" );
    }
    i = get_int();
    return true;
}
bool FlexJsonValue::read( int64_t &i, bool throw_on_error ) const
{
    if( !test_number() ) {
        return error_or_false( throw_on_error, "Expected number" );
    }
    i = get_int64();
    return true;
}
bool FlexJsonValue::read( uint64_t &i, bool throw_on_error ) const
{
    if( !test_number() ) {
        return error_or_false( throw_on_error, "Expected number" );
    }
    i = get_uint64();
    return true;
}
bool FlexJsonValue::read( unsigned int &u, bool throw_on_error ) const
{
    if( !test_number() ) {
        return error_or_false( throw_on_error, "Expected number" );
    }
    u = get_uint();
    return true;
}
bool FlexJsonValue::read( float &f, bool throw_on_error ) const
{
    if( !test_number() ) {
        return error_or_false( throw_on_error, "Expected number" );
    }
    f = get_float();
    return true;
}
bool FlexJsonValue::read( double &d, bool throw_on_error ) const
{
    if( !test_number() ) {
        return error_or_false( throw_on_error, "Expected number" );
    }
    d = get_float();
    return true;
}
bool FlexJsonValue::read( std::string &s, bool throw_on_error ) const
{
    if( !test_string() ) {
        return error_or_false( throw_on_error, "Expected string" );
    }
    s = get_string();
    return true;
}
