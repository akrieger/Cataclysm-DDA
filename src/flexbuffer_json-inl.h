#pragma once
#ifndef CATA_SRC_FLEXBUFFER_JSON_INL_H
#define CATA_SRC_FLEXBUFFER_JSON_INL_H

#include <string>
#include <type_traits>

#include <flatbuffers/flexbuffers.h>

#include "cata_bitset.h"
#include "cata_small_literal_vector.h"
#include "filesystem.h"
#include "flexbuffer_cache.h"
#include "json.h"
#include "json_error.h"
#include "int_id.h"
#include "memory_fast.h"
#include "optional.h"
#include "string_id.h"

std::string const &FlexJson::flexbuffer_type_to_string( flexbuffers::Type t )
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

// The iterators have to come first because clang requires complete definitions when we call begin/end in definitions later in the header.

class FlexJsonArray::const_iterator
{
        const_iterator( FlexJsonArray const &object, size_t pos )
            : array_{ object }, pos_{ pos }
        {}

    public:
        const_iterator &operator++() {
            pos_++;
            return *this;
        }

        bool operator==( const_iterator const &other ) const {
            return pos_ == other.pos_ && &array_ == &other.array_;
        }

        bool operator!=( const_iterator const &other ) const {
            return !( *this == other );
        }

        FlexJsonValue operator*() const {
            array_.mark_visited( pos_ );

            return FlexJsonValue( array_[ pos_ ] );
        }

    private:
        FlexJsonArray const &array_;
        size_t pos_;

        friend FlexJsonArray;
};

inline auto FlexJsonArray::begin() const -> const_iterator
{
    return const_iterator( *this, 0 );
}

inline auto FlexJsonArray::end() const -> const_iterator
{
    return const_iterator( *this, size_ );
}

class FlexJsonObject::const_iterator
{
        const_iterator( FlexJsonObject const &object, size_t pos )
            : object_{ object }, pos_{ pos }
        {}

    public:
        const_iterator &operator++() {
            pos_++;
            return *this;
        }

        bool operator==( const_iterator const &other ) const {
            return pos_ == other.pos_ && &object_ == &other.object_;
        }

        bool operator!=( const_iterator const &other ) const {
            return !( *this == other );
        }

        FlexJsonMember operator*() const {
            object_.mark_visited( pos_ );
            return FlexJsonMember(
                       object_.keys_[pos_].AsString(),
                       object_[pos_] );
        }

    private:
        FlexJsonObject const &object_;
        size_t pos_;

        friend FlexJsonObject;
};

inline auto FlexJsonObject::begin() const -> const_iterator
{
    return const_iterator( *this, 0 );
}

inline auto FlexJsonObject::end() const -> const_iterator
{
    return const_iterator( *this, keys_.size() );
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

inline FlexJsonValue::operator int64_t() const
{
    if( json_.IsNumeric() ) {
        return static_cast<int64_t>( json_.AsInt64() );
    }
    throw_error( "Expected an int64_t, got a " + std::to_string( json_.GetType() ) );
}

inline FlexJsonValue::operator uint64_t() const
{
    if( json_.IsNumeric() ) {
        // These are always stored as signed ints.
        int64_t signed_value = json_.AsInt64();
        if( signed_value >= 0 ) {
            return static_cast<uint64_t>( signed_value );
        }
        throw_error( "uint64_t value out of range" );
    }
    throw_error( "Expected a uint64_t, got a " + std::to_string( json_.GetType() ) );
}

inline FlexJsonValue::operator unsigned() const
{
    if( json_.IsNumeric() ) {
        // These are always stored as signed ints.
        int64_t signed_value = json_.AsInt64();
        if( signed_value >= 0 ) {
            return static_cast<unsigned int>( signed_value );
        }
        throw_error( "unsigned int value out of range" );
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
    JsonPath new_path;
    if( parent_path_ ) {
        new_path = *parent_path_ + path_index_;
    }
    if( json_.IsMap() ) {
        return FlexJsonObject( root_, flexbuffer( json_ ), std::move( new_path ) );
    }
    throw_error( new_path, "Expected an object, got a " + std::to_string( json_.GetType() ) );
}

inline FlexJsonValue::operator FlexJsonArray() const
{
    JsonPath new_path;
    if( parent_path_ ) {
        new_path = *parent_path_ + path_index_;
    }
    if( json_.IsAnyVector() && !json_.IsMap() ) {
        return FlexJsonArray( root_, flexbuffer( json_ ), std::move( new_path ) );
    }
    throw_error( new_path, "Expected an array, got a " + std::to_string( json_.GetType() ) );
}

inline bool FlexJsonValue::test_string() const
{
    return json_.IsString();
}
inline bool FlexJsonValue::test_bool() const
{
    return json_.IsBool();
}
inline bool FlexJsonValue::test_number() const
{
    return json_.IsNumeric();
}
inline bool FlexJsonValue::test_int() const
{
    return json_.IsNumeric();
}
inline bool FlexJsonValue::test_float() const
{
    return json_.IsNumeric();
}
inline bool FlexJsonValue::test_object() const
{
    return json_.IsMap();
}
inline bool FlexJsonValue::test_array() const
{
    return json_.IsVector() && !json_.IsMap();
}
inline bool FlexJsonValue::test_null() const
{
    return json_.IsNull();
}

inline std::string FlexJsonValue::get_string() const
{
    return static_cast<std::string>( *this );
}
inline bool FlexJsonValue::get_bool() const
{
    return static_cast<bool>( *this );
}
inline int FlexJsonValue::get_int() const
{
    return static_cast<int>( *this );
}
inline unsigned int FlexJsonValue::get_uint() const
{
    return static_cast<unsigned int>( *this );
}
inline int64_t FlexJsonValue::get_int64() const
{
    return static_cast<int64_t>( *this );
}
inline uint64_t FlexJsonValue::get_uint64() const
{
    return static_cast<uint64_t>( *this );
}
inline double FlexJsonValue::get_float() const
{
    return static_cast<double>( *this );
}

inline FlexJsonArray FlexJsonValue::get_array() const
{
    return static_cast<FlexJsonArray>( *this );
}

inline FlexJsonObject FlexJsonValue::get_object() const
{
    return static_cast<FlexJsonObject>( *this );
}

// This is for the string_id type
template <typename T>
auto FlexJsonValue::read( string_id<T> &thing, bool throw_on_error ) const -> bool
{
    std::string tmp;
    if( !read( tmp, throw_on_error ) ) {
        return false;
    }
    thing = string_id<T>( tmp );
    return true;
}

// This is for the int_id type
template <typename T>
auto FlexJsonValue::read( int_id<T> &thing, bool throw_on_error ) const -> bool
{
    std::string tmp;
    if( !read( tmp, throw_on_error ) ) {
        return false;
    }
    thing = int_id<T>( tmp );
    return true;
}

/// Overload that calls a global function `deserialize(T&,JsonIn&)`, if available.
template<typename T>
auto FlexJsonValue::read( T &v, bool throw_on_error ) const ->
decltype( deserialize( v, *this ), true )
{
    try {
        deserialize( v, *this );
        return true;
    } catch( const JsonError & ) {
        if( throw_on_error ) {
            throw;
        }
        return false;
    }
}

/// Overload that calls a member function `T::deserialize(JsonIn&)`, if available.
template<typename T>
auto FlexJsonValue::read( T &v, bool throw_on_error ) const -> decltype( v.deserialize( *this ),
        true )
{
    try {
        v.deserialize( *this );
        return true;
    } catch( const JsonError & ) {
        if( throw_on_error ) {
            throw;
        }
        return false;
    }
}

template<typename T, std::enable_if_t<std::is_enum<T>::value, int>>
bool FlexJsonValue::read( T &val, bool throw_on_error ) const
{
    int i;
    if( read( i, false ) ) {
        val = static_cast<T>( i );
        return true;
    }
    std::string s;
    if( read( s, throw_on_error ) ) {
        val = io::string_to_enum<T>( s );
        return true;
    }
    return false;
}

/// Overload for std::pair
template<typename T, typename U>
bool FlexJsonValue::read( std::pair<T, U> &p, bool throw_on_error ) const
{
    if( !test_array() ) {
        return error_or_false( throw_on_error, "Expected json array encoding pair" );
    }
    try {
        FlexJsonArray ja = get_array();
        if( ja.size() != 2 ) {
            return error_or_false( throw_on_error, "Array had wrong number of elements for pair" );
        }
        bool result = ja[ 0 ].read( p.first, throw_on_error ) &&
                      ja[ 1 ].read( p.second, throw_on_error );
        if( !result && throw_on_error ) {
            throw_error( "Array had wrong number of elements for pair" );
        }
        return result;
    } catch( const JsonError & ) {
        if( throw_on_error ) {
            throw;
        }
        return false;
    }
}

// array ~> vector, deque, list
template < typename T, typename std::enable_if <
               !std::is_same<void, typename T::value_type>::value >::type * >
auto FlexJsonValue::read( T &v, bool throw_on_error ) const -> decltype( v.front(), true )
{
    if( !test_array() ) {
        return error_or_false( throw_on_error, "Expected json array" );
    }
    try {
        v.clear();
        for( FlexJsonValue jv : get_array() ) {
            typename T::value_type element;
            if( jv.read( element, throw_on_error ) ) {
                v.push_back( std::move( element ) );
            }
        }
    } catch( const JsonError & ) {
        if( throw_on_error ) {
            throw;
        }
        return false;
    }

    return true;
}

// array ~> array
template <typename T, size_t N>
bool FlexJsonValue::read( std::array<T, N> &v, bool throw_on_error ) const
{
    if( !test_array() ) {
        return error_or_false( throw_on_error, "Expected json array" );
    }
    try {
        FlexJsonArray ja = get_array();
        if( ja.size() != N ) {
            if( ja.size() < N ) {
                return error_or_false( throw_on_error, "Json array is too short" );
            } else {
                return error_or_false( throw_on_error, "Json array is too long" );
            }
        }
        size_t i = 0;
        for( FlexJsonValue jv : ja ) {
            if( !jv.read( v[ i ], throw_on_error ) ) {
                return false; // invalid entry
            }
            ++i;
        }
        return true;
    } catch( const JsonError & ) {
        if( throw_on_error ) {
            throw;
        }
        return false;
    }
}

// object ~> containers with matching key_type and value_type
// set, unordered_set ~> object
template <typename T, typename std::enable_if<
              std::is_same<typename T::key_type, typename T::value_type>::value>::type *>
bool FlexJsonValue::read( T &v, bool throw_on_error ) const
{
    if( !test_array() ) {
        return error_or_false( throw_on_error, "Expected json array" );
    }
    try {
        v.clear();
        for( FlexJsonValue jv : get_array() ) {
            typename T::value_type element;
            if( jv.read( element, throw_on_error ) ) {
                v.insert( std::move( element ) );
            }
        }
    } catch( const JsonError & ) {
        if( throw_on_error ) {
            throw;
        }
        return false;
    }

    return true;
}

// special case for colony<item> as it supports RLE
// see corresponding `write` for details
template <typename T, std::enable_if_t<std::is_same<T, item>::value>* >
bool FlexJsonValue::read( cata::colony<T> &v, bool throw_on_error ) const
{
    if( !test_array() ) {
        return error_or_false( throw_on_error, "Expected json array" );
    }
    try {
        v.clear();
        for( FlexJsonValue jv : get_array() ) {
            T element;
            if( jv.test_array() ) {
                FlexJsonArray rle_element = jv;
                if( rle_element.size() != 2 ) {
                    rle_element.error_or_false( throw_on_error, "Not enough values for RLE" );
                    continue;
                }
                int run_l;
                if( rle_element[ 0 ].read( element, throw_on_error ) &&
                    rle_element[ 1 ].read( run_l, throw_on_error )
                  ) { // all is good
                    // first insert (run_l-1) elements
                    for( int i = 0; i < run_l - 1; i++ ) {
                        v.insert( element );
                    }
                    // micro-optimization, move last element
                    v.insert( std::move( element ) );
                } else { // array is malformed, skipping it entirely
                    error_or_false( throw_on_error, "Expected end of array" );
                }
            } else {
                if( jv.read( element, throw_on_error ) ) {
                    v.insert( std::move( element ) );
                }
            }
        }
    } catch( const JsonError & ) {
        if( throw_on_error ) {
            throw;
        }
        return false;
    }

    return true;
}

// special case for colony as it uses `insert()` instead of `push_back()`
// and therefore doesn't fit with vector/deque/list
// for colony of items there is another specialization with RLE
template < typename T, std::enable_if_t < !std::is_same<T, item>::value > * >
bool FlexJsonValue::read( cata::colony<T> &v, bool throw_on_error ) const
{
    if( !test_array() ) {
        return error_or_false( throw_on_error, "Expected json array" );
    }
    try {
        v.clear();
        for( FlexJsonValue jv : get_array() ) {
            typename cata::colony<T>::value_type element;
            if( jv.read( element, throw_on_error ) ) {
                v.insert( std::move( element ) );
            }
        }
    } catch( const JsonError & ) {
        if( throw_on_error ) {
            throw;
        }
        return false;
    }

    return true;
}

// object ~> containers with unmatching key_type and value_type
// map, unordered_map ~> object
template < typename T, typename std::enable_if <
               !std::is_same<typename T::key_type, typename T::value_type>::value >::type * >
bool FlexJsonValue::read( T &m, bool throw_on_error ) const
{
    if( !test_object() ) {
        return error_or_false( throw_on_error, "Expected json object" );
    }
    try {
        m.clear();
        for( FlexJsonMember jm : get_object() ) {
            using key_type = typename T::key_type;
            key_type key = key_from_json_string<key_type>()( jm.name() );
            typename T::mapped_type element;
            if( jm.read( element, throw_on_error ) ) {
                m[ std::move( key ) ] = std::move( element );
            }
        }
    } catch( const JsonError & ) {
        if( throw_on_error ) {
            throw;
        }
        return false;
    }

    return true;
}

// array ~> vector, deque, list
template < typename T, typename std::enable_if <
               !std::is_same<void, typename T::value_type>::value >::type * >
auto FlexJsonArray::read( T &v, bool throw_on_error ) -> decltype( v.front(), true ) const
{
    try {
        v.clear();
        for( FlexJsonValue value : *this ) {
            typename T::value_type element;
            if( value.read( element, throw_on_error ) ) {
                v.emplace_back( std::move( element ) );
            }
        }
    } catch( const JsonError & ) {
        if( throw_on_error ) {
            throw;
        }
        return false;
    }

    return true;
}

inline FlexJsonValue FlexJsonArray::operator[]( size_t idx ) const
{
    // Manually bsearch for the key idx to store in visited_fields_bitset_.
    // flexbuffers::Map::operator[] will probably be faster but won't give us the idx,
    // which we need to track visited fields.

    if( idx < size_ ) {
        mark_visited( idx );
        flexbuffers::Reference value;
        if( json_.IsFixedTypedVector() ) {
            value = json_.AsFixedTypedVector()[idx];
        } else if( json_.IsTypedVector() ) {
            value = json_.AsTypedVector()[idx];
        } else {
            value = json_.AsVector()[idx];
        }
        return FlexJsonValue{ root_, std::move( value ), &path_, idx };
    }
    throw_error( std::to_string( idx ) + " index is out of bounds." );
}

inline std::string FlexJsonArray::get_string( size_t idx ) const
{
    return ( *this )[ idx ];
}

inline int FlexJsonArray::get_int( size_t idx ) const
{
    return ( *this )[ idx ];
}

inline double FlexJsonArray::get_float( size_t idx ) const
{
    return ( *this )[ idx ];
}

inline FlexJsonArray FlexJsonArray::get_array( size_t idx ) const
{
    return ( *this )[ idx ];
}

inline FlexJsonObject FlexJsonArray::get_object( size_t idx ) const
{
    return ( *this )[ idx ];
}

inline bool FlexJsonArray::has_string( size_t idx ) const
{
    return ( *this )[ idx ].test_string();
}

inline bool FlexJsonArray::has_bool( size_t idx ) const
{
    return ( *this )[idx].test_bool();
}

inline bool FlexJsonArray::has_int( size_t idx ) const
{
    return ( *this )[idx].test_int();
}

inline bool FlexJsonArray::has_float( size_t idx ) const
{
    return ( *this )[idx].test_float();
}

inline bool FlexJsonArray::has_array( size_t idx ) const
{
    return ( *this )[ idx ].test_array();
}

inline bool FlexJsonArray::has_object( size_t idx ) const
{
    return ( *this )[idx].test_object();
}

inline bool FlexJsonArray::test_string() const
{
    return has_string( next_ );
}
inline std::string FlexJsonArray::next_string()
{
    return get_next();
}

inline bool FlexJsonArray::test_bool() const
{
    return has_bool( next_ );
}
inline bool FlexJsonArray::next_bool()
{
    return get_next();
}

inline bool FlexJsonArray::test_int() const
{
    return has_int( next_ );
}
inline int FlexJsonArray::next_int()
{
    return get_next();
}

inline bool FlexJsonArray::test_float() const
{
    return has_float( next_ );
}

inline double FlexJsonArray::next_float()
{
    return get_next();
}

inline bool FlexJsonArray::test_array() const
{
    return has_array( next_ );
}
inline FlexJsonArray FlexJsonArray::next_array()
{
    return get_next();
}

inline bool FlexJsonArray::test_object() const
{
    return has_object( next_ );
}
inline FlexJsonObject FlexJsonArray::next_object()
{
    return get_next();
}

inline FlexJsonValue FlexJsonArray::next_value()
{
    return get_next();
}

template<typename E, typename>
E FlexJsonArray::next_enum_value()
{
    try {
        return io::string_to_enum<E>( next_string() );
    } catch( const io::InvalidEnumString & ) {
        --path_.last();
        throw_error( "invalid enumeration value" );
    }
}

// random-access read values by reference
template <typename T> bool FlexJsonArray::read_next( T &t, bool throw_on_error )
{
    return get_next().read( t, throw_on_error );
}

// random-access read values by reference
template <typename T> bool FlexJsonArray::read( size_t idx, T &t, bool throw_on_error ) const
{
    return ( *this )[ idx ].read( t, throw_on_error );
}

[[noreturn]] inline void FlexJsonArray::string_error( const std::string &message, size_t idx,
        int offset ) const
{
    ( *this )[ idx ].string_error( path_, message, offset );
}

template <typename T, typename Res>
Res FlexJsonArray::get_tags( const size_t idx ) const
{
    Res res;

    FlexJsonValue jv = ( *this )[ idx ];

    // allow single string as tag
    if( jv.test_string() ) {
        res.insert( T( jv.get_string() ) );
        return res;
    }

    for( const std::string line : jv.get_array() ) {
        res.insert( T( line ) );
    }

    return res;
}

inline FlexJsonValue FlexJsonArray::get_next()
{
    return ( *this )[ next_++ ];
}

inline std::string FlexJsonObject::get_string( std::string const &key ) const
{
    return get_string( key.c_str() );
}
inline std::string FlexJsonObject::get_string( const char *key ) const
{
    return get_member( key );
}

template<typename T, typename std::enable_if_t<std::is_convertible<T, std::string>::value>*>
std::string FlexJsonObject::get_string( std::string const &key, T &&fallback ) const
{
    return get_string( key.c_str(), std::forward<T>( fallback ) );
}

template<typename T, typename std::enable_if_t<std::is_convertible<T, std::string>::value>*>
std::string FlexJsonObject::get_string( const char *key, T &&fallback ) const
{
    size_t idx = 0;
    bool found = find_map_key_idx( key, keys_, idx );
    if( found ) {
        return get_string( key );
    }
    return std::forward<T>( fallback );
}

// Vanilla accessors. Just return the named member and use it's conversion function.
inline int FlexJsonObject::get_int( std::string const &key ) const
{
    return get_member( key.c_str() );
}
inline int FlexJsonObject::get_int( const char *key ) const
{
    return get_member( key );
}

inline double FlexJsonObject::get_float( std::string const &key ) const
{
    return get_member( key.c_str() );
}
inline double FlexJsonObject::get_float( const char *key ) const
{
    return get_member( key );
}

inline bool FlexJsonObject::get_bool( std::string const &key ) const
{
    return get_member( key.c_str() );
}
inline bool FlexJsonObject::get_bool( const char *key ) const
{
    return get_member( key );
}

inline FlexJsonArray FlexJsonObject::get_array( std::string const &key ) const
{
    return get_array( key.c_str() );
}

inline FlexJsonArray FlexJsonObject::get_array( const char *key ) const
{
    auto member_opt = get_member_opt( key );
    if( member_opt.has_value() ) {
        return std::move( *member_opt );
    }
    return FlexJsonArray{};
}

inline FlexJsonObject FlexJsonObject::get_object( std::string const &key ) const
{
    return get_object( key.c_str() );
}

inline FlexJsonObject FlexJsonObject::get_object( const char *key ) const
{
    auto member_opt = get_member_opt( key );
    if( member_opt.has_value() ) {
        return std::move( *member_opt );
    }
    return FlexJsonObject{};
}

template<typename E, typename >
E FlexJsonObject::get_enum_value( const std::string &name ) const
{
    return get_enum_value<E>( name.c_str() );
}
template<typename E, typename >
E FlexJsonObject::get_enum_value( const char *name ) const
{
    auto value = get_member( name );
    try {
        return io::string_to_enum<E>( static_cast<std::string>( value ) );
    } catch( const io::InvalidEnumString & ) {
        value.throw_error( "invalud enumumeration value" );
    }
}

template<typename E, typename >
E FlexJsonObject::get_enum_value( const std::string &name, E fallback ) const
{
    return get_enum_value<E>( name.c_str(), fallback );
}
template<typename E, typename >
E FlexJsonObject::get_enum_value( const char *name, E fallback ) const
{
    auto value = get_member_opt( name );
    if( value.has_value() ) {
        try {
            return io::string_to_enum<E>( static_cast<std::string>( *value ) );
        } catch( const io::InvalidEnumString & ) {
            value->throw_error( "invalud enumumeration value" );
        }
    } else {
        return fallback;
    }
}

inline std::vector<int> FlexJsonObject::get_int_array( const std::string &name ) const
{
    std::vector<int> ret;
    FlexJsonArray ja = get_array( name );
    ret.reserve( ja.size() );
    for( FlexJsonValue jv : get_array( name ) ) {
        ret.emplace_back( jv );
    }
    return ret;
}
inline std::vector<std::string> FlexJsonObject::get_string_array( const std::string &name ) const
{
    std::vector<std::string> ret;
    FlexJsonArray ja = get_array( name );
    ret.reserve( ja.size() );
    for( FlexJsonValue jv : get_array( name ) ) {
        ret.emplace_back( jv );
    }
    return ret;
}

inline bool FlexJsonObject::has_member( std::string const &key ) const
{
    return has_member( key.c_str() );
}

inline bool FlexJsonObject::has_member( const char *key ) const
{
    size_t idx;
    return find_map_key_idx( key, keys_, idx );
}

inline bool FlexJsonObject::has_null( const char *key ) const
{
    auto ref = find_value_ref( key );
    return ref.IsNull();
}
inline bool FlexJsonObject::has_null( const std::string &key ) const
{
    return has_null( key.c_str() );
}

inline bool FlexJsonObject::has_int( const char *key ) const
{
    return has_number( key );
}
inline bool FlexJsonObject::has_int( const std::string &key ) const
{
    return has_number( key );
}

inline bool FlexJsonObject::has_float( const char *key ) const
{
    return has_number( key );
}
inline bool FlexJsonObject::has_float( const std::string &key ) const
{
    return has_number( key );
}

inline bool FlexJsonObject::has_number( const char *key ) const
{
    auto ref = find_value_ref( key );
    return ref.IsNumeric();
}
inline bool FlexJsonObject::has_number( const std::string &key ) const
{
    return has_number( key.c_str() );
}

inline bool FlexJsonObject::has_string( std::string const &key ) const
{
    return has_string( key.c_str() );
}

inline bool FlexJsonObject::has_string( const char *key ) const
{
    auto ref = find_value_ref( key );
    return ref.IsString();
}

inline bool FlexJsonObject::has_bool( const std::string &key ) const
{
    return has_bool( key.c_str() );
}
inline bool FlexJsonObject::has_bool( const char *key ) const
{
    auto ref = find_value_ref( key );
    return ref.IsBool();
}

inline bool FlexJsonObject::has_array( std::string const &key ) const
{
    return has_array( key.c_str() );
}

inline bool FlexJsonObject::has_array( const char *key ) const
{
    auto ref = find_value_ref( key );
    return ref.IsAnyVector() && !ref.IsMap();
}

inline bool FlexJsonObject::has_object( const char *key ) const
{
    auto ref = find_value_ref( key );
    return ref.IsMap();
}
inline bool FlexJsonObject::has_object( const std::string &key ) const
{
    return has_object( key.c_str() );
}

// Fallback accessors. Test if the named member exists, and if yes, return it,
// else will return the fallback value. Does *not* test the member is the type
// being requested.
inline int FlexJsonObject::get_int( std::string const &key, int fallback ) const
{
    return get_int( key.c_str(), fallback );
}
inline int FlexJsonObject::get_int( const char *key, int fallback ) const
{
    auto member_opt = get_member_opt( key );
    if( member_opt.has_value() ) {
        return *member_opt;
    }
    return fallback;
}

inline double FlexJsonObject::get_float( std::string const &key, double fallback ) const
{
    return get_float( key.c_str(), fallback );
}
inline double FlexJsonObject::get_float( const char *key, double fallback ) const
{
    auto member_opt = get_member_opt( key );
    if( member_opt.has_value() ) {
        return *member_opt;
    }
    return fallback;
}

inline bool FlexJsonObject::get_bool( std::string const &key, bool fallback ) const
{
    return get_bool( key.c_str(), fallback );
}
inline bool FlexJsonObject::get_bool( const char *key, bool fallback ) const
{
    auto member_opt = get_member_opt( key );
    if( member_opt.has_value() ) {
        return *member_opt;
    }
    return fallback;
}

// Tries to get the member, and if found, calls it visited.
inline cata::optional<FlexJsonValue> FlexJsonObject::get_member_opt( const char *key ) const
{
    size_t idx = 0;
    bool found = find_map_key_idx( key, keys_, idx );
    if( found ) {
        mark_visited( idx );
        return FlexJsonValue{ root_, values_[idx], &path_, idx };
    }
    return cata::nullopt;
}

inline FlexJsonValue FlexJsonObject::get_member( const std::string &key ) const
{
    return get_member( key.c_str() );
}
inline FlexJsonValue FlexJsonObject::get_member( const char *key ) const
{
    // Manually bsearch for the key idx to store in visited_fields_bitset_.
    // flexbuffers::Map::operator[] will probably be faster but won't give us the idx,
    // which we need to track visited fields.
    size_t idx = 0;
    bool found = find_map_key_idx( key, keys_, idx );
    if( found ) {
        mark_visited( idx );
        return FlexJsonValue{ root_, values_[ idx ], &path_, idx };
    }
    error_no_member( key );
    return ( *this )[key];
}

inline FlexJsonValue FlexJsonObject::operator[]( const char *key ) const
{
    return get_member( key );
}

template <typename T>
bool FlexJsonObject::read( const char *name, T &t, bool throw_on_error ) const
{
    auto member_opt = get_member_opt( name );
    if( !member_opt.has_value() ) {
        return false;
    }
    return ( *member_opt ).read( t, throw_on_error );
}
template <typename T>
bool FlexJsonObject::read( const std::string &name, T &t, bool throw_on_error ) const
{
    return read( name.c_str(), t, throw_on_error );
}

template <typename T, typename Res>
Res FlexJsonObject::get_tags( const std::string &name ) const
{
    return get_tags<T, Res>( name.c_str() );
}

template <typename T, typename Res>
Res FlexJsonObject::get_tags( const char *name ) const
{
    Res res;
    cata::optional<FlexJsonValue> member_opt = get_member_opt( name );
    if( !member_opt.has_value() ) {
        return res;
    }

    FlexJsonValue &member = *member_opt;

    // allow single string as tag
    if( member.test_string() ) {
        res.insert( T{ static_cast<std::string>( member ) } );
        return res;
    }

    // otherwise assume it's an array and error if it isn't.
    for( const std::string line : static_cast<FlexJsonArray>( member ) ) {
        res.insert( T( line ) );
    }

    return res;
}

[[noreturn]] inline void FlexJsonObject::throw_error( const std::string &err,
        int offset ) const
{
    FlexJson::throw_error( path_, err, offset );
}

[[noreturn]] inline void FlexJsonObject::throw_error( const std::string &err,
        const std::string &member,
        int offset ) const
{
    throw_error( err, member.c_str(), offset );
}

[[noreturn]] inline void FlexJsonObject::throw_error( const std::string &err, const char *member,
        int offset ) const
{
    auto member_opt = get_member_opt( member );
    if( member_opt.has_value() ) {
        ( *member_opt ).throw_error( err, offset );
    } else {
        throw_error( err, offset );
    }
}

inline FlexJsonValue FlexJsonObject::operator[]( size_t idx ) const
{
    mark_visited( idx );
    return FlexJsonValue{ root_, values_[idx], &path_, idx };
}

inline void FlexJsonObject::report_unvisited() const
{
    if( !visited_fields_bitset_.all() ) {
        std::vector<size_t> skipped_members;
        skipped_members.reserve( visited_fields_bitset_.size() );
        auto *bits = visited_fields_bitset_.bits();
        size_t block_idx = 0;
        for( size_t last_whole_block = visited_fields_bitset_.size() / TinyBitSet::kBitsPerBlock;
             block_idx < last_whole_block; ++block_idx ) {
            TinyBitSet::BlockT block = bits[ block_idx ];
            TinyBitSet::BlockT mask = TinyBitSet::ONE << ( TinyBitSet::kBitsPerBlock - 1 );
            for( size_t bit_idx = 0; bit_idx < TinyBitSet::kBitsPerBlock; ++bit_idx ) {
                if( block & mask ) {
                    skipped_members.emplace_back( block_idx * TinyBitSet::kBitsPerBlock + bit_idx );
                }
                mask >>= 1;
            }
        }

        TinyBitSet::BlockT block = bits[ block_idx ];
        TinyBitSet::BlockT mask = TinyBitSet::ONE << ( TinyBitSet::kBitsPerBlock - 1 );
        for( size_t bit_idx = 0, end = visited_fields_bitset_.size() % TinyBitSet::kBitsPerBlock;
             bit_idx < end; ++bit_idx ) {
            if( block & mask ) {
                skipped_members.emplace_back( block_idx * TinyBitSet::kBitsPerBlock + bit_idx );
            }
            mask >>= 1;
        }

        error_skipped_members( skipped_members );
    }
}

template<typename T>
void deserialize( cata::optional<T> &obj, const FlexJsonValue &jsin )
{
    if( jsin.test_null() ) {
        obj.reset();
    } else {
        obj.emplace();
        jsin.read( *obj, true );
    }
}

inline void add_array_to_set( std::set<std::string> &s, const FlexJsonObject &json,
                              const std::string &name )
{
    for( const std::string line : json.get_array( name ) ) {
        s.insert( line );
    }
}

#endif // CATA_SRC_FLEXBUFFER_JSON_INL_H
