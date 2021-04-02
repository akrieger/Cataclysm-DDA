#pragma once
#ifndef CATA_SRC_FLEXBUFFER_JSON_INL_H
#define CATA_SRC_FLEXBUFFER_JSON_INL_H

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
        return FlexJsonObject( FlexBuffer( json_ ), std::string( source_file_ ), JsonPath( path_ ) );
    }
    throw_error( "Expected an object, got a " + std::to_string( json_.GetType() ) );
}

inline FlexJsonValue::operator FlexJsonArray() const
{
    if( json_.IsAnyVector() && !json_.IsMap() ) {
        return FlexJsonArray( FlexBuffer{ json_ }, std::string( source_file_ ), JsonPath( path_ ) );
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
