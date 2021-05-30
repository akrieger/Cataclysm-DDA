#include "flexbuffer_json.h"

#include <fstream>

#include "json.h"

// Advances the given jsin according to the passed JsonPath
static void advance_jsin( TextJsonIn *jsin, flexbuffers::Reference root, JsonPath const &path )
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

void JsonIn::error( const std::string &message, int offset )
{
    std::ifstream original_json( *source_file_, std::ifstream::in | std::ifstream::binary );
    TextJsonIn jsin( original_json );

    advance_jsin( &jsin, *root_, path_ );

    jsin.error( message, offset );
}

void JsonIn::string_error( const std::string &message, int offset )
{
    std::ifstream original_json( *source_file_, std::ifstream::in | std::ifstream::binary );
    TextJsonIn jsin( original_json );

    advance_jsin( &jsin, *root_, path_ );

    jsin.string_error( message, offset );
}

void Json::throw_error( std::string const &message, int offset ) const
{
    std::ifstream original_json( source_file_, std::ifstream::in | std::ifstream::binary );
    TextJsonIn jsin( original_json );

    advance_jsin( &jsin, /*root_*/flexbuffers::Reference(), path_ );

    jsin.error( message, offset );
}

void Json::string_error(std::string const& message, int offset) const
{
    std::ifstream original_json(source_file_, std::ifstream::in | std::ifstream::binary);
    TextJsonIn jsin(original_json);

    advance_jsin(&jsin, /*root_*/flexbuffers::Reference(), path_);

    jsin.string_error(message, offset);
}

std::string Json::str() const
{
    std::string ret;
    json_.ToString( false, true, ret );
    return ret;
}

void JsonObject::error_no_member( std::string const &member ) const
{
    std::ifstream original_json( source_file_, std::ifstream::in | std::ifstream::binary );
    TextJsonIn jsin( original_json );

    advance_jsin( &jsin, /*root_*/ flexbuffers::Reference(), path_ );

    TextJsonObject jo = jsin.get_object();
    jo.allow_omitted_members();
    jo.get_member( member );
    // Just to make sure the compiler understands we will error earlier.
    jo.throw_error( "Failed to report missing member " + member );
}

void JsonObject::error_skipped_members( std::vector<size_t> const &skipped_members ) const
{
    std::ifstream original_json( source_file_, std::ifstream::in | std::ifstream::binary );
    TextJsonIn jsin( original_json );

    advance_jsin( &jsin, /*root_*/flexbuffers::Reference(), path_ );

    TextJsonObject jo = jsin.get_object();
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

json_source_location JsonObject::get_source_location() const
{
    if( source_file_.empty() ) {
        throw_error( "JsonObject::get_source_location called but the path is unknown" );
    }

    std::ifstream original_json( source_file_, std::ifstream::in | std::ifstream::binary );
    TextJsonIn jsin( original_json );

    advance_jsin( &jsin, /*root_*/flexbuffers::Reference(), path_ );

    json_source_location loc;
    loc.path = make_shared_fast<std::string>( source_file_ );
    loc.offset = jsin.tell();
    return loc;
}

bool JsonIn::read(bool& b, bool throw_on_error) {
    if( !test_bool() ) {
        return error_or_false(throw_on_error, "Expected bool");
    }
    b = get_bool();
    return true;
}
bool JsonIn::read(char& c, bool throw_on_error) {
    if( !test_number() ) {
        return error_or_false(throw_on_error, "Expected number");
    }
    c = get_int();
    return true;
}
bool JsonIn::read(signed char& c, bool throw_on_error) {
    if( !test_number() ) {
        return error_or_false(throw_on_error, "Expected number");
    }
    // TODO: test for overflow
    c = get_int();
    return true;
}
bool JsonIn::read(unsigned char& c, bool throw_on_error) {
    if( !test_number() ) {
        return error_or_false(throw_on_error, "Expected number");
    }
    // TODO: test for overflow
    c = get_int();
    return true;
}
bool JsonIn::read(short unsigned int& s, bool throw_on_error) {
    if( !test_number() ) {
        return error_or_false(throw_on_error, "Expected number");
    }
    // TODO: test for overflow
    s = get_int();
    return true;
}
bool JsonIn::read(short int& s, bool throw_on_error) {
    if( !test_number() ) {
        return error_or_false(throw_on_error, "Expected number");
    }
    // TODO: test for overflow
    s = get_int();
    return true;
}
bool JsonIn::read(int& i, bool throw_on_error) {
    if( !test_number() ) {
        return error_or_false(throw_on_error, "Expected number");
    }
    i = get_int();
    return true;
}
bool JsonIn::read(int64_t& i, bool throw_on_error) {
    if( !test_number() ) {
        return error_or_false(throw_on_error, "Expected number");
    }
    i = get_int64();
    return true;
}
bool JsonIn::read(uint64_t& i, bool throw_on_error) {
    if( !test_number() ) {
        return error_or_false(throw_on_error, "Expected number");
    }
    i = get_uint64();
    return true;
}
bool JsonIn::read(unsigned int& u, bool throw_on_error) {
    if( !test_number() ) {
        return error_or_false(throw_on_error, "Expected number");
    }
    u = get_uint();
    return true;
}
bool JsonIn::read(float& f, bool throw_on_error) {
    if( !test_number() ) {
        return error_or_false(throw_on_error, "Expected number");
    }
    f = get_float();
    return true;
}
bool JsonIn::read(double& d, bool throw_on_error) {
    if( !test_number() ) {
        return error_or_false(throw_on_error, "Expected number");
    }
    d = get_float();
    return true;
}
bool JsonIn::read(std::string& s, bool throw_on_error) {
    if( !test_string() ) {
        return error_or_false(throw_on_error, "Expected string");
    }
    s = get_string();
    return true;
}
bool JsonIn::read(JsonDeserializer& j, bool throw_on_error) {
    try {
        j.deserialize(*this);
        return true;
    } catch( const JsonError& ) {
        if( throw_on_error ) {
            throw;
        }
        return false;
    }
}
