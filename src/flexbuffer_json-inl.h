#pragma once
#ifndef CATA_SRC_FLEXBUFFER_JSON_INL_H
#define CATA_SRC_FLEXBUFFER_JSON_INL_H

inline JsonValue JsonIn::get_value()
{
    check_idx_before_deref();
    flexbuffers::Reference value = get_current();
    JsonValue ret{ std::move(value), *source_file_, JsonPath(path_) };
    advance();
    return ret;
}

inline JsonObject JsonIn::get_object()
{
    check_idx_before_deref();
    flexbuffers::Reference value = get_current();
    if( value.IsMap() ) {
        JsonObject ret{ std::move(value), *source_file_, JsonPath(path_) };
        advance();
        return ret;
    }
    throw std::runtime_error( "Not an object" );
}

inline JsonArray JsonIn::get_array()
{
    check_idx_before_deref();
    flexbuffers::Reference value = get_current();
    if( value.IsVector() && !value.IsMap() ) {
        JsonArray ret{ std::move(value), *source_file_, JsonPath(path_) };
        advance();
        return ret;
    }
    throw std::runtime_error( "Not an array" );
}

std::string const &JsonIn::flexbuffer_type_to_string( flexbuffers::Type t )
{
    static const std::array<std::string, 36> type_map = { {
            "null",
            "int",
            "uint",
            "float",
            "key",
            "string",
            "indirect_int",
            "indirect_uint",
            "indirect_float",
            "object",
            "array",
            "int_array",
            "uint_array",
            "float_array",
            "key_array",
            "string_array_deprecated",
            "int_tuple_array",
            "uint_tuple_array",
            "float_tuple_array",
            "int_triple_array",
            "uint_triple_array",
            "float_triple_array",
            "int_quad_array",
            "uint_quad_array",
            "float_quad_array",
            "blob",
            "bool",
            "bool_array",
        }
    };
    return type_map[static_cast<unsigned int>( t )];
}

inline JsonValue::operator std::string() const
{
    if( json_.IsString() ) {
        return json_.AsString().str();
    }
    throw_error( "Expected a string, got a " + std::to_string( json_.GetType() ) );
}

inline JsonValue::operator int() const
{
    if( json_.IsNumeric() ) {
        return static_cast<int>( json_.AsInt64() );
    }
    throw_error( "Expected an int, got a " + std::to_string( json_.GetType() ) );
}
inline JsonValue::operator bool() const
{
    if( json_.IsBool() ) {
        return json_.AsBool();
    }
    throw_error( "Expected a bool, got a " + std::to_string( json_.GetType() ) );
}

inline JsonValue::operator double() const
{
    if( json_.IsNumeric() ) {
        return json_.AsDouble();
    }
    throw_error( "Expected a double, got a " + std::to_string( json_.GetType() ) );
}
inline JsonValue::operator JsonObject() const
{
    if( json_.IsMap() ) {
        return JsonObject( FlexBuffer( json_ ), std::string( source_file_ ), JsonPath( path_ ) );
    }
    throw_error( "Expected an object, got a " + std::to_string( json_.GetType() ) );
}

inline JsonValue::operator JsonArray() const
{
    if( json_.IsAnyVector() && !json_.IsMap() ) {
        return JsonArray( FlexBuffer{ json_ }, std::string( source_file_ ), JsonPath( path_ ) );
    }
    throw_error( "Expected an array, got a " + std::to_string( json_.GetType() ) );
}

inline JsonObject JsonValue::get_object() const {
    return (JsonObject)( *this );
}

inline std::vector<int> JsonObject::get_int_array(const std::string& name) const {
    std::vector<int> ret;
    JsonArray ja = get_array(name);
    ret.reserve(ja.size());
    for (JsonValue jv : get_array(name)) {
        ret.emplace_back(jv);
    }
    return ret;
}
inline std::vector<std::string> JsonObject::get_string_array(const std::string& name) const {
    std::vector<std::string> ret;
    JsonArray ja = get_array(name);
    ret.reserve(ja.size());
    for( JsonValue jv : get_array(name) ) {
        ret.emplace_back(jv);
    }
    return ret;
}

inline JsonArray JsonValue::get_array() const {
    return (JsonArray)( *this );
}

inline JsonArray JsonObject::get_array( std::string const &key ) const
{
    return get_array( key.c_str() );
}

inline JsonArray JsonObject::get_array( const char *key ) const
{
    return get_member( key );
}

#endif // CATA_SRC_FLEXBUFFER_JSON_INL_H
