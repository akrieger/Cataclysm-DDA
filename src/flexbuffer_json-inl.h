#pragma once
#ifndef CATA_SRC_FLEXBUFFER_JSON_INL_H
#define CATA_SRC_FLEXBUFFER_JSON_INL_H

inline FlexJsonObject FlexJsonIn::get_object()
{
    check_idx_before_deref();
    flexbuffers::Reference value = get_current();
    if( value.IsMap() ) {
        advance();
        return FlexJsonObject( std::move( value ), *source_file_, FlexJsonPath( path_ ) );
    }
    throw std::runtime_error( "Not an object" );
}

inline FlexJsonArray FlexJsonIn::get_array()
{
    check_idx_before_deref();
    flexbuffers::Reference value = get_current();
    if( value.IsVector() && !value.IsMap() ) {
        advance();
        return FlexJsonArray( std::move( value ), *source_file_, FlexJsonPath( path_ ) );
    }
    throw std::runtime_error( "Not an array" );
}

std::string const &FlexJsonIn::flexbuffer_type_to_string( flexbuffers::Type t )
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

inline FlexJsonValue::operator std::string() const
{
    if( json_.IsString() ) {
        return json_.AsString().str();
    }
    throw_error( "Expected a string, got a " + std::to_string( json_.GetType() ) );
}

inline FlexJsonValue::operator int() const
{
    if( json_.IsNumeric() ) {
        return static_cast<int>( json_.AsInt64() );
    }
    throw_error( "Expected an int, got a " + std::to_string( json_.GetType() ) );
}
inline FlexJsonValue::operator bool() const
{
    if( json_.IsBool() ) {
        return json_.AsBool();
    }
    throw_error( "Expected a bool, got a " + std::to_string( json_.GetType() ) );
}

inline FlexJsonValue::operator double() const
{
    if( json_.IsNumeric() ) {
        return json_.AsDouble();
    }
    throw_error( "Expected a double, got a " + std::to_string( json_.GetType() ) );
}
inline FlexJsonValue::operator FlexJsonObject() const
{
    if( json_.IsMap() ) {
        return FlexJsonObject( FlexBuffer( json_ ), std::string( source_file_ ), FlexJsonPath( path_ ) );
    }
    throw_error( "Expected an object, got a " + std::to_string( json_.GetType() ) );
}

inline FlexJsonValue::operator FlexJsonArray() const
{
    if( json_.IsAnyVector() && !json_.IsMap() ) {
        return FlexJsonArray( FlexBuffer{ json_ }, std::string( source_file_ ), FlexJsonPath( path_ ) );
    }
    throw_error( "Expected an array, got a " + std::to_string( json_.GetType() ) );
}

inline FlexJsonArray FlexJsonObject::get_array( std::string const &key ) const
{
    return get_array( key.c_str() );
}

inline FlexJsonArray FlexJsonObject::get_array( const char *key ) const
{
    return get_member( key );
}

#endif // CATA_SRC_FLEXBUFFER_JSON_INL_H
