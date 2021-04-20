#pragma once
#ifndef CATA_SRC_FLEXBUFFER_JSON_H
#define CATA_SRC_FLEXBUFFER_JSON_H

#include <string>
#include <type_traits>

#include "flatbuffers/flexbuffers.h"

#include "cata_bitset.h"
#include "cata_small_literal_vector.h"
#include "flexbuffer_cache.h"
#include "json.h"
#include "int_id.h"
#include "optional.h"
#include "string_id.h"

// A tag type representing a string interned in a flexbuffer. We can use this to do custom comparisons that might be faster, should they be needed.
class FlexString
{
    public:
        FlexString( flexbuffers::String &&str ) : str_( std::move( str ) ) {}

        bool operator==( FlexString const &rhs ) {
            return str_.c_str() == rhs.str_.c_str();
        }

    private:
        int compare( const char *str, size_t len, size_t cmplen ) {
            if( str_.c_str() == str ) {
                return 0;
            }
            int cmp = strncmp( str_.c_str(), str, cmplen );
            return cmp == 0 ? ( len < str_.length() ? 1 : -1 ) : cmp;
        }

        flexbuffers::String str_;
};

// Represents a 'path' in a json object, a series of object keys or indices, that when accessed from the root get you to some element in the json structure.
struct JsonPath {
        // Stores either a numeric index for an array or a string key for a dictionary.
        // Typically we'd use a union here but there's no way to (ab)use bits to store
        // the underlying kind of the variant. Instead, we use this heuristic: if `key`
        // is nullptr, then it's an idx, else key is a pointer to a string and idx is
        // the length of the string. This keeps the size fixed to 8 or 16 bytes for
        // 32 or 64 bit, with an 8 byte alignment.
        // Keeping it trivial enables more easier shenanigans later.
        struct Element {
            const char *key;
            size_t idx;

            inline bool is_string() const {
                return key != nullptr;
            }

            std::string as_string() const {
                return std::string( key, idx );
            }

            inline bool is_index() const {
                return key == nullptr;
            }

            size_t as_index() const {
                return idx;
            }
        };

        JsonPath() = default;
        JsonPath( JsonPath const & ) = default;
        JsonPath( JsonPath && ) = default;

        JsonPath &operator=( JsonPath const & ) = default;
        JsonPath &operator=( JsonPath && ) = default;

        JsonPath( JsonPath const &other, size_t idx ) {
            path_.concat( path_, Element{ nullptr, idx } );
        }

        inline JsonPath operator+( size_t idx ) const {
            JsonPath ret;
            ret.path_.concat( path_, Element{  nullptr, idx} );
            return ret;
        }

        inline JsonPath operator+( const char *key ) const {
            JsonPath ret;
            ret.path_.concat( path_, Element{ key, strlen( key ) } );
            return ret;
        }

        inline JsonPath operator+( flexbuffers::String const &key ) const {
            JsonPath ret;
            ret.path_.concat( path_, Element{ key.c_str(), key.length() } );
            return ret;
        }

        Element const *begin() const {
            return path_.begin();
        }
        Element const *end() const {
            return path_.end();
        }

    private:
        // 3 keeps the size of FlexJsonValue down to 120 bytes on 64 bit.
        static constexpr size_t kInlinePathSegments = 3;
        SmallLiteralVector<Element, kInlinePathSegments> path_;
};

// Represents a 'path' in a json object, a series of object keys or indices, that when accessed from the root get you to some element in the json structure.
struct FlexJsonPath {
        // Flexbuffer structures are either arrays or objects, where objects are just
        // arrays with an extra 'key vector'. As such, we can store a jsonpath as a series
        // of numeric indices and not store any string pointer values at all. This lets us
        // compress the datastructure to a series of shorts (at least one vector has >1k
        // elements in it). How this index should be interpreted depends on the type of
        // the json value at that location. Arrays as native indices, objects as an index
        // into the object's corresponding key vector.

        FlexJsonPath() = default;
        FlexJsonPath( FlexJsonPath const & ) = default;
        FlexJsonPath( FlexJsonPath && ) = default;

        FlexJsonPath &operator=( FlexJsonPath const & ) = default;
        FlexJsonPath &operator=( FlexJsonPath && ) = default;

        inline FlexJsonPath &push_back( size_t idx ) {
            if( idx < std::numeric_limits<uint16_t>::max() ) {
                path_.push_back( static_cast<uint16_t>( idx ) );
            } else {
                throw std::runtime_error( "Json index out of range of uint16_t" );
            }
        }

        inline void pop_back() {
            path_.pop_back();
        }

        uint16_t &last() {
            return *path_.back();
        }

        uint16_t const *begin() const {
            return path_.begin();
        }
        uint16_t const *end() const {
            return path_.end();
        }

    private:
        static constexpr size_t kInlinePathSegments = 7;
        SmallLiteralVector<uint16_t, kInlinePathSegments> path_;
};

// A replacement for JsonIn that operates on a FlexBuffer, storing position with a FlexJsonPath.
// We leverage the current design of JsonIn & friends to maximize efficiency of the Flex equivalents.
// For example: JsonObject keeps a JsonIn*, so FlexJsonObject can keep a JsonIn&, and behave accordingly.
class FlexJsonIn
{
        using FlexBuffer = FlexBufferCache::FlexBuffer;
        // Keeps the backing memory for the flexbuffer alive, whether in builder memory or an mmap'd file.
        // We need this to interpret path_ when throwing an error.
        std::shared_ptr<FlexBuffer> root_;
        // The parent of the value pointed to by path_. Empty path -> root reference.
        // Unfortunately popping out of an object requires re-traversing from the root.
        flexbuffers::Reference parent_;
        // WHy inline a string when you can outline it for a saved pointer in size?
        shared_ptr_fast<std::string> source_file_;
        // Path to the current element.
        FlexJsonPath path_;
        // Number of values in parent_.
        uint16_t values_in_parent_;

    public:
#define THROW_UNIMPLEMENTED throw std::runtime_error("unimplemented")
        shared_ptr_fast<std::string> get_path() const {
            return source_file_;
        }

        // get current stream position
        int tell() {
            THROW_UNIMPLEMENTED;
        }

        // seek to specified stream position
        void seek( int pos ) {
            THROW_UNIMPLEMENTED;
        }

        // what's the next char gonna be?
        char peek() {
            THROW_UNIMPLEMENTED;
        }

        // whether stream is ok
        bool good() {
            THROW_UNIMPLEMENTED;
        }

        // advance seek head to the next non-whitespace character
        void eat_whitespace() {
            THROW_UNIMPLEMENTED;
        }
        // or rewind to the previous one
        void uneat_whitespace() {
            THROW_UNIMPLEMENTED;
        }

        // quick skipping for when values don't have to be parsed
        void skip_member() {
            THROW_UNIMPLEMENTED;
        }
        void skip_string() {
            THROW_UNIMPLEMENTED;
        }
        void skip_value() {
            THROW_UNIMPLEMENTED;
        }
        void skip_object() {
            THROW_UNIMPLEMENTED;
        }
        void skip_array() {
            THROW_UNIMPLEMENTED;
        }
        void skip_true() {
            THROW_UNIMPLEMENTED;
        }
        void skip_false() {
            THROW_UNIMPLEMENTED;
        }
        void skip_null() {
            THROW_UNIMPLEMENTED;
        }
        void skip_number() {
            THROW_UNIMPLEMENTED;
        }

        size_t count_values_in_parent() {
            if( parent_.IsVector() ) {
                return parent_.AsVector().size();
            }
            throw std::runtime_error( "Parent is not keyed type." );
        }

        void advance() {
            if( path_.last() + 1 == values_in_parent_ ) {
                end_iterable();
            }

        }

        void end_iterable() {
            path_.pop_back();
            parent_ = *root_;
            for( uint16_t idx : path_ ) {
                if( parent_.IsMap() ) {
                    parent_ = parent_.AsVector()[ idx ];
                }
            }
        }

        // data parsing
        // get the next value as a string
        std::string get_string() {
            auto idx = path_.last();
            if( parent_.IsAnyVector() && !parent_.IsMap() ) {
            }
        }
        // get the next value as an int
        int get_int();
        // get the next value as an unsigned int
        unsigned int get_uint();
        // get the next value as an int64
        int64_t get_int64();
        // get the next value as a uint64
        uint64_t get_uint64();
        // get the next value as a bool
        bool get_bool();
        // get the next value as a double
        double get_float();
        // also strips the ':'
        std::string get_member_name();

        JsonObject get_object();
        JsonArray get_array();

        template<typename E, typename = typename std::enable_if<std::is_enum<E>::value>::type>
        E get_enum_value() {
            const int old_offset = tell();
            try {
                return io::string_to_enum<E>( get_string() );
            } catch( const io::InvalidEnumString & ) {
                seek( old_offset ); // so the error message points to the correct place.
                error( "invalid enumeration value" );
            }
        }

        // container control and iteration
        void start_array(); // verify array start
        bool end_array(); // returns false if it's not the end
        void start_object();
        bool end_object(); // returns false if it's not the end

        // type testing
        bool test_null();
        bool test_bool();
        bool test_number();
        bool test_int() {
            return test_number();
        }
        bool test_float() {
            return test_number();
        }
        bool test_string();
        bool test_bitset();
        bool test_array();
        bool test_object();

        // optionally-fatal reading into values by reference
        // returns true if the data was read successfully, false otherwise
        // if throw_on_error then throws JsonError rather than returning false.
        bool read( bool &b, bool throw_on_error = false );
        bool read( char &c, bool throw_on_error = false );
        bool read( signed char &c, bool throw_on_error = false );
        bool read( unsigned char &c, bool throw_on_error = false );
        bool read( short unsigned int &s, bool throw_on_error = false );
        bool read( short int &s, bool throw_on_error = false );
        bool read( int &i, bool throw_on_error = false );
        bool read( int64_t &i, bool throw_on_error = false );
        bool read( uint64_t &i, bool throw_on_error = false );
        bool read( unsigned int &u, bool throw_on_error = false );
        bool read( float &f, bool throw_on_error = false );
        bool read( double &d, bool throw_on_error = false );
        bool read( std::string &s, bool throw_on_error = false );
        template<size_t N>
        bool read( std::bitset<N> &b, bool throw_on_error = false );
        bool read( JsonDeserializer &j, bool throw_on_error = false );
        // This is for the string_id type
        template <typename T>
        auto read( T &thing, bool throw_on_error = false ) -> decltype( thing.str(), true ) {
            std::string tmp;
            if( !read( tmp, throw_on_error ) ) {
                return false;
            }
            thing = T( tmp );
            return true;
        }

        // This is for the int_id type
        template <typename T>
        auto read( int_id<T> &thing, bool throw_on_error = false ) -> bool {
            std::string tmp;
            if( !read( tmp, throw_on_error ) ) {
                return false;
            }
            thing = int_id<T>( tmp );
            return true;
        }

        /// Overload that calls a global function `deserialize(T&,JsonIn&)`, if available.
        template<typename T>
        auto read( T &v, bool throw_on_error = false ) ->
        decltype( deserialize( v, *this ), true ) {
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
        auto read( T &v, bool throw_on_error = false ) -> decltype( v.deserialize( *this ), true ) {
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

        template<typename T, std::enable_if_t<std::is_enum<T>::value, int> = 0>
        bool read( T &val, bool throw_on_error = false ) {
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
        bool read( std::pair<T, U> &p, bool throw_on_error = false ) {
            if( !test_array() ) {
                return error_or_false( throw_on_error, "Expected json array encoding pair" );
            }
            try {
                start_array();
                bool result = !end_array() &&
                              read( p.first, throw_on_error ) &&
                              !end_array() &&
                              read( p.second, throw_on_error ) &&
                              end_array();
                if( !result && throw_on_error ) {
                    error( "Array had wrong number of elements for pair" );
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
                       !std::is_same<void, typename T::value_type>::value >::type * = nullptr
                   >
        auto read( T &v, bool throw_on_error = false ) -> decltype( v.front(), true ) {
            if( !test_array() ) {
                return error_or_false( throw_on_error, "Expected json array" );
            }
            try {
                start_array();
                v.clear();
                while( !end_array() ) {
                    typename T::value_type element;
                    if( read( element, throw_on_error ) ) {
                        v.push_back( std::move( element ) );
                    } else {
                        skip_value();
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
        bool read( std::array<T, N> &v, bool throw_on_error = false ) {
            if( !test_array() ) {
                return error_or_false( throw_on_error, "Expected json array" );
            }
            try {
                start_array();
                for( size_t i = 0; i < N; ++i ) {
                    if( end_array() ) {
                        if( throw_on_error ) {
                            error( "Json array is too short" );
                        }
                        return false; // json array is too small
                    }
                    if( !read( v[ i ], throw_on_error ) ) {
                        return false; // invalid entry
                    }
                }
                bool result = end_array();
                if( !result && throw_on_error ) {
                    error( "Array had too many elements" );
                }
                return result;
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
                      std::is_same<typename T::key_type, typename T::value_type>::value>::type * = nullptr
                  >
        bool read( T &v, bool throw_on_error = false ) {
            if( !test_array() ) {
                return error_or_false( throw_on_error, "Expected json array" );
            }
            try {
                start_array();
                v.clear();
                while( !end_array() ) {
                    typename T::value_type element;
                    if( read( element, throw_on_error ) ) {
                        v.insert( std::move( element ) );
                    } else {
                        skip_value();
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
        template <typename T, std::enable_if_t<std::is_same<T, item>::value>* = nullptr >
        bool read( cata::colony<T> &v, bool throw_on_error = false ) {
            if( !test_array() ) {
                return error_or_false( throw_on_error, "Expected json array" );
            }
            try {
                start_array();
                v.clear();
                while( !end_array() ) {
                    T element;
                    const int prev_pos = tell();
                    if( test_array() ) {
                        start_array();
                        int run_l;
                        if( read( element, throw_on_error ) &&
                            read( run_l, throw_on_error ) &&
                            end_array()
                          ) { // all is good
                            // first insert (run_l-1) elements
                            for( int i = 0; i < run_l - 1; i++ ) {
                                v.insert( element );
                            }
                            // micro-optimization, move last element
                            v.insert( std::move( element ) );
                        } else { // array is malformed, skipping it entirely
                            error_or_false( throw_on_error, "Expected end of array" );
                            seek( prev_pos );
                            skip_array();
                        }
                    } else {
                        if( read( element, throw_on_error ) ) {
                            v.insert( std::move( element ) );
                        } else {
                            skip_value();
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
        template < typename T, std::enable_if_t < !std::is_same<T, item>::value > * = nullptr >
        bool read( cata::colony<T> &v, bool throw_on_error = false ) {
            if( !test_array() ) {
                return error_or_false( throw_on_error, "Expected json array" );
            }
            try {
                start_array();
                v.clear();
                while( !end_array() ) {
                    T element;
                    if( read( element, throw_on_error ) ) {
                        v.insert( std::move( element ) );
                    } else {
                        skip_value();
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
                       !std::is_same<typename T::key_type, typename T::value_type>::value >::type * = nullptr
                   >
        bool read( T &m, bool throw_on_error = true ) {
            if( !test_object() ) {
                return error_or_false( throw_on_error, "Expected json object" );
            }
            try {
                start_object();
                m.clear();
                while( !end_object() ) {
                    using key_type = typename T::key_type;
                    key_type key = key_from_json_string<key_type>()( get_member_name() );
                    typename T::mapped_type element;
                    if( read( element, throw_on_error ) ) {
                        m[ std::move( key ) ] = std::move( element );
                    } else {
                        skip_value();
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

        // error messages
        std::string line_number( int offset_modifier = 0 ); // for occasional use only
        [[noreturn]] void error( const std::string &message, int offset = 0 ); // ditto
        // if the next element is a string, throw error after the `offset`th unicode
        // character in the parsed string. if `offset` is 0, throw error right after
        // the starting quotation mark.
        [[noreturn]] void string_error( const std::string &message, int offset );

        // If throw_, then call error( message, offset ), otherwise return
        // false
        bool error_or_false( bool throw_, const std::string &message, int offset = 0 );
        void rewind( int max_lines = -1, int max_chars = -1 );
        std::string substr( size_t pos, size_t len = std::string::npos );
};

class FlexJson
{
    public:
        FlexJson() = delete;
        using FlexBuffer = FlexBufferCache::FlexBuffer;

        std::string str() const;
        [[noreturn]] void throw_error( std::string const &message ) const;

    protected:
        FlexJson( FlexBuffer &&json, std::string &&source_file ) : json_{ std::move( json ) }, source_file_{ std::move( source_file ) } {}
        FlexJson( FlexBuffer &&json, std::string &&source_file, JsonPath &&path ) : json_{std::move( json )},
            source_file_{ std::move( source_file ) },
            path_{ std::move( path ) } {}

        virtual ~FlexJson() = default;

        // All flexbuffer subclasses must keep the backing flexbuffer alive.
        FlexBuffer json_;
        std::string source_file_;
        JsonPath path_;

        flexbuffers::Reference error_or_null( bool throw_on_error,
                                              std::string const &message ) const noexcept( false ) {
            if( throw_on_error ) {
                throw_error( message );
            }
            return flexbuffers::Reference();
        }

        bool error_or_false( bool throw_on_error, std::string const &message ) const noexcept( false ) {
            if( throw_on_error ) {
                throw_error( message );
            }
            return false;
        }
};

class FlexJsonObject;
class FlexJsonArray;

// A single value wrapper. Convertible to any actual value or array/object..
class FlexJsonValue : FlexJson
{
    public:
        static cata::optional<FlexJsonValue> fromOpt( std::string source_file ) noexcept( false ) {
            std::shared_ptr<FlexBuffer> buffer = FlexBufferCache::global_cache().parse_and_cache( source_file );
            if( !buffer ) {
                return cata::nullopt;
            }
            return FlexJsonValue( std::move( *buffer ), std::move( source_file ) );
        }

        static FlexJsonValue from( std::string source_file ) noexcept( false ) {
            std::shared_ptr<FlexBuffer> buffer = FlexBufferCache::global_cache().parse_and_cache( source_file );
            if( !buffer ) {
                throw JsonError( "Didn't get a flexbuffer but didn't get an exception either." );
            }
            return FlexJsonValue( std::move( *buffer ), std::move( source_file ) );
        }

        FlexJsonValue( FlexBuffer &&json, std::string &&source_file ) : FlexJson( std::move( json ),
                    std::move( source_file ) ) {}
        FlexJsonValue( FlexBuffer &&json, std::string &&source_file,
                       JsonPath &&path ) : FlexJson( std::move( json ), std::move( source_file ), std::move( path ) ) {}

        // NOLINTNEXTLINE(google-explicit-constructor)
        inline operator std::string() const;
        // NOLINTNEXTLINE(google-explicit-constructor)
        inline operator int() const;
        // NOLINTNEXTLINE(google-explicit-constructor)
        inline operator bool() const;
        // NOLINTNEXTLINE(google-explicit-constructor)
        inline operator double() const;
        // NOLINTNEXTLINE(google-explicit-constructor)
        inline operator FlexJsonObject() const;
        // NOLINTNEXTLINE(google-explicit-constructor)
        inline operator FlexJsonArray() const;

        // optionally-fatal reading into values by reference
        // returns true if the data was read successfully, false otherwise
        // if throw_on_error then throws JsonError rather than returning false.
        bool read( bool &b, bool throw_on_error = false ) const;
        bool read( char &c, bool throw_on_error = false ) const;
        bool read( signed char &c, bool throw_on_error = false ) const;
        bool read( unsigned char &c, bool throw_on_error = false ) const;
        bool read( short unsigned int &s, bool throw_on_error = false ) const;
        bool read( short int &s, bool throw_on_error = false ) const;
        bool read( int &i, bool throw_on_error = false ) const;
        bool read( int64_t &i, bool throw_on_error = false ) const;
        bool read( uint64_t &i, bool throw_on_error = false ) const;
        bool read( unsigned int &u, bool throw_on_error = false ) const;
        bool read( float &f, bool throw_on_error = false ) const;
        bool read( double &d, bool throw_on_error = false ) const;
        bool read( std::string &s, bool throw_on_error = false ) const;
        template<size_t N>
        bool read( std::bitset<N> &b, bool throw_on_error = false );

        using FlexJson::throw_error;
};

class FlexJsonMember : public FlexJsonValue
{
    public:
        FlexJsonMember( flexbuffers::String name,
                        FlexJsonValue value ) : FlexJsonValue( std::move( value ) ),
            name_( name ) { }

        std::string name() const {
            return name_.str();
        }

        bool is_comment() const {
            return strncmp( name_.c_str(), "//", 2 ) == 0;
        }

    private:
        flexbuffers::String name_;
};

class FlexJsonObject : FlexJson
{
    public:
        using FlexBuffer = FlexBufferCache::FlexBuffer;

    protected:
        flexbuffers::TypedVector keys_ = flexbuffers::TypedVector::EmptyTypedVector();
        flexbuffers::Vector values_ = flexbuffers::Vector::EmptyVector();
        mutable TinyBitSet visited_fields_bitset_;

    public:

        FlexJsonObject(
            FlexBuffer &&json,
            std::string source_file )
            : FlexJson( std::move( json ), std::move( source_file ) ) {
            init( json_ );
        }

        FlexJsonObject(
            FlexBuffer &&json,
            std::string source_file,
            JsonPath &&path )
            : FlexJson( std::move( json ), std::move( source_file )
            , std::move( path ) ) {
            init( json_ );
        }

        FlexJsonObject &operator=( FlexJsonObject const & ) = default;

    private:
        void init( FlexBuffer const &json ) {
            auto json_map = json.AsMap();
            keys_ = json_map.Keys();
            values_ = json_map.Values();
            visited_fields_bitset_.resize( keys_.size() );
        }
    public:
        std::string get_string( std::string const &key ) const {
            return get_string( key.c_str() );
        }
        std::string get_string( const char *key ) const {
            return get_member( key );
        }

        template<typename T, typename std::enable_if_t<std::is_convertible_v<T, std::string>>* = nullptr>
        std::string get_string( std::string const &key, T && fallback ) const {
            return get_string( key.c_str(), std::forward<T>( fallback ) );
        }

        template<typename T, typename std::enable_if_t<std::is_convertible_v<T, std::string>>* = nullptr>
        std::string get_string( const char *key, T && fallback ) const {
            size_t idx = 0;
            bool found = find_map_key_idx( key, keys_, idx );
            if( found ) {
                return get_string( key );
            }
            return std::forward<T>( fallback );
        }

        // Vanilla accessors. Just return the named member and use it's conversion function.
        int get_int( std::string const &key ) const {
            return get_member( key.c_str() );
        }
        int get_int( const char *key ) const {
            return get_member( key );
        }

        bool get_bool( std::string const &key ) const {
            return get_member( key.c_str() );
        }
        bool get_bool( const char *key ) const {
            return get_member( key );
        }

        inline FlexJsonArray get_array( std::string const &key ) const;
        inline FlexJsonArray get_array( const char *key ) const;

        FlexJsonObject get_object( std::string const &key ) const {
            return get_member( key.c_str() );
        }

        FlexJsonObject get_object( const char *key ) const {
            return get_member( key );
        }

        bool has_member( std::string const &key ) const {
            return has_member( key.c_str() );
        }

        bool has_member( const char *key ) const {
            size_t idx;
            return find_map_key_idx( key, keys_, idx );
        }

        bool has_number( const char *key ) const {
            auto ref = find_value_ref( key );
            return ref.IsNumeric();
        }

        bool has_string( std::string const &key ) const {
            return has_string( key.c_str() );
        }

        bool has_string( const char *key ) const {
            auto ref = find_value_ref( key );
            return ref.IsString();
        }

        bool has_array( std::string const &key ) const {
            return has_array( key.c_str() );
        }

        bool has_array( const char *key ) const {
            auto ref = find_value_ref( key );
            return ref.IsAnyVector() && !ref.IsMap();
        }

        bool has_object( const char *key ) const {
            auto ref = find_value_ref( key );
            return ref.IsMap();
        }

        // Fallback accessors. Test if the named member exists, and if yes, return it,
        // else will return the fallback value. Does *not* test the member is the type
        // being requested.
        int get_int( std::string const &key, int fallback ) const {
            return get_int( key.c_str(), fallback );
        }
        int get_int( const char *key, int fallback ) const {
            auto member_opt = get_member_opt( key );
            if( member_opt.has_value() ) {
                return *member_opt;
            }
            return fallback;
        }

        bool get_bool( std::string const &key, bool fallback ) const {
            return get_bool( key.c_str(), fallback );
        }
        bool get_bool( const char *key, bool fallback ) const {
            auto member_opt = get_member_opt( key );
            if( member_opt.has_value() ) {
                return *member_opt;
            }
            return fallback;
        }

        // Tries to get the member, and if found, calls it visited.
        cata::optional<FlexJsonValue> get_member_opt( const char *key ) const {
            size_t idx = 0;
            bool found = find_map_key_idx( key, keys_, idx );
            if( found ) {
                mark_visited( idx );
                return FlexJsonValue{ values_[idx], std::string( source_file_ ), path_ + key };
            }
            return cata::nullopt;
        }

        FlexJsonValue get_member( const char *key ) const {
            // Manually bsearch for the key idx to store in visited_fields_bitset_.
            // flexbuffers::Map::operator[] will probably be faster but won't give us the idx,
            // which we need to track visited fields.
            size_t idx = 0;
            bool found = find_map_key_idx( key, keys_, idx );
            if( found ) {
                mark_visited( idx );
                return FlexJsonValue{ values_[idx], std::string( source_file_ ), path_ + key };
            }
            error_no_member( key );
            return ( *this )[key];
        }

        FlexJsonValue operator[]( const char *key ) const {
            return get_member( key );
        }

        // Schwillions of read overloads
        template <typename T>
        bool read( const char *name, T &t, bool throw_on_error = true ) const {
            auto member_opt = get_member_opt( name );
            if( !member_opt.has_value() ) {
                return false;
            }
            return ( *member_opt ).read( t, throw_on_error );
        }
        template <typename T>
        bool read( const std::string &name, T &t, bool throw_on_error = true ) const {
            return read( name.c_str(), t, throw_on_error );
        }

        using FlexJson::throw_error;
        [[noreturn]] void throw_error( const std::string &err, const std::string &member ) const {
            throw_error( err, member.c_str() );
        }
        [[noreturn]] void throw_error( const std::string &err, const char *member ) const {
            auto member_opt = get_member_opt( member );
            if( member_opt.has_value() ) {
                ( *member_opt ).throw_error( err );
            }
        }

        void allow_omitted_members() const {
            visited_fields_bitset_.set_all();
        }

        json_source_location get_source_location() const;

        using FlexJson::str;

    public:
        class const_iterator
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

        friend const_iterator;

        const_iterator begin() const {
            return const_iterator( *this, 0 );
        }

        const_iterator end() const {
            return const_iterator( *this, keys_.size() );
        }

    private:
        // Only called by the iterator which can't be manually constructed.
        FlexJsonValue operator[]( size_t idx ) const {
            mark_visited( idx );
            const char *key = keys_[idx].AsKey();
            return FlexJsonValue{ values_[idx], std::string( source_file_ ), path_ + key };
        }

        flexbuffers::Reference find_value_ref( const char *key ) const {
            size_t idx = 0;
            bool found = find_map_key_idx( key, keys_, idx );
            if( found ) {
                return values_[ idx ];
            }
            return flexbuffers::Reference();
        }

        static bool find_map_key_idx( const char *key, flexbuffers::TypedVector const &keys, size_t &idx ) {
            // Handlrolled binary search because the STL does not provide a version that just uses indexes.
            typename std::make_signed<size_t>::type low = 0;
            typename std::make_signed<size_t>::type high = keys.size() - 1;
            while( low <= high ) {
                std::make_signed_t<size_t> mid = ( high - low ) / 2 + low;

                auto test_key = keys[ mid ].AsKey();
                auto res = strcmp( test_key, key );

                if( res == 0 ) {
                    idx = mid;
                    return true;
                } else if( res < 0 ) {
                    low = mid + 1;
                } else {
                    high = mid - 1;
                }
            }
            return false;
        }

        void mark_visited( size_t idx ) const {
            visited_fields_bitset_.set( idx );
        }

        void report_unvisited() const {
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

        // Reports an error via JsonObject at this location.
        [[noreturn]] void error_no_member( std::string const &member ) const;
        // debugmsg prints all the skipped members.
        void error_skipped_members( std::vector<size_t> const &skipped_members ) const;
};

// An iterable wrapper around FlexJson that always iterates values only.
class FlexJsonArray : FlexJson
{
    public:
        FlexJsonArray(
            FlexBuffer &&json,
            std::string source_file )
            : FlexJson( std::move( json ), std::move( source_file ) ) {
            init( json_ );
        }

        FlexJsonArray(
            FlexBuffer &&json,
            std::string source_file,
            JsonPath &&path )
            : FlexJson( std::move( json ), std::move( source_file )
            , std::move( path ) ) {
            init( json_ );
        }

        // array ~> vector, deque, list
        template < typename T, typename std::enable_if <
                       !std::is_same<void, typename T::value_type>::value >::type * = nullptr
                   >
        auto read( T &v, bool throw_on_error = false ) -> decltype( v.front(), true ) {
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

        class const_iterator
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

        friend const_iterator;

        const_iterator begin() const {
            return const_iterator( *this, 0 );
        }

        const_iterator end() const {
            return const_iterator( *this, size_ );
        }

        int get_int( size_t idx ) const {
            return ( *this )[ idx ];
        }

        size_t size() const {
            return size_;
        }

    private:
        size_t size_;

        void init( FlexBuffer const &json ) {
            if( json.IsFixedTypedVector() ) {
                auto json_vec = json.AsFixedTypedVector();
                size_ = json_vec.size();
            } else if( json.IsTypedVector() ) {
                auto json_vec = json.AsTypedVector();
                size_ = json_vec.size();
            } else {
                auto json_vec = json.AsVector();
                size_ = json_vec.size();
            }
            visited_fields_bitset_.resize( size_ );
        }

        FlexJsonValue operator[]( size_t idx ) const {
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
                return FlexJsonValue{ std::move( value ), std::string( source_file_ ), path_ + idx };
            }
            throw_error( std::to_string( idx ) + " index is out of bounds." );
        }

        mutable TinyBitSet visited_fields_bitset_;

        void mark_visited( size_t idx ) const {
            visited_fields_bitset_.set( idx );
        }
};

#include "flexbuffer_json-inl.h"

#endif // CATA_SRC_FLEXBUFFER_JSON_H
