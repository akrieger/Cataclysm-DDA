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
#include "memory_fast.h"
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
        // Flexbuffer structures are either arrays or objects, where objects are just
        // arrays with an extra 'key vector'. As such, we can store a jsonpath as a series
        // of numeric indices and not store any string pointer values at all. This lets us
        // compress the datastructure to a series of shorts (at least one vector has >1k
        // elements in it). How this index should be interpreted depends on the type of
        // the json value at that location. Arrays as native indices, objects as an index
        // into the object's corresponding key vector.

        JsonPath() = default;
        JsonPath( JsonPath const & ) = default;
        JsonPath( JsonPath && ) = default;

        JsonPath &operator=( JsonPath const & ) = default;
        JsonPath &operator=( JsonPath && ) = default;

        inline JsonPath &push_back( size_t idx ) {
            if( idx < std::numeric_limits<uint16_t>::max() ) {
                path_.push_back( static_cast<uint16_t>( idx ) );
            } else {
                throw std::runtime_error( "Json index out of range of uint16_t" );
            }
            return *this;
        }

        uint16_t size() const {
            return path_.size();
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

        friend JsonPath operator+( JsonPath const &lhs, size_t idx ) {
            JsonPath ret( lhs );
            ret.push_back( idx );
            return ret;
        }

    private:
        static constexpr size_t kInlinePathSegments = 7;
        SmallLiteralVector<uint16_t, kInlinePathSegments> path_;
};

class JsonObject;
class JsonArray;
class JsonValue;

// A replacement for JsonIn that operates on a FlexBuffer, storing position with a JsonPath.
// We leverage the current design of JsonIn & friends to maximize efficiency of the Flex equivalents.
// For example: JsonObject keeps a JsonIn*, so JsonObject can keep a JsonIn&, and behave accordingly.
class JsonIn
{
        using FlexBuffer = FlexBufferCache::FlexBuffer;
        // Keeps the backing memory for the flexbuffer alive, whether in builder memory or an mmap'd file.
        // We need this to interpret path_ when throwing an error.
        std::shared_ptr<FlexBuffer> root_;
        // The parent of the value pointed to by path_. Empty path -> root reference.
        // Unfortunately popping out of an object requires re-traversing from the root.
        // This must always be a Vector (or a Map, which is a Vector), aka a keyed type.
        flexbuffers::Reference parent_;
        // Why inline a string when you can outline it for a saved pointer in size?
        shared_ptr_fast<std::string> source_file_;
        // Path to the current element.
        JsonPath path_;
        // Number of values in parent_.
        uint16_t values_in_parent_;

    public:
        static cata::optional<JsonIn> fromOpt( std::string source_file ) noexcept( false ) {
            std::shared_ptr<FlexBuffer> buffer = FlexBufferCache::global_cache().parse_and_cache( source_file );
            if( !buffer ) {
                return cata::nullopt;
            }
            return JsonIn( std::move( buffer ), make_shared_fast<std::string>( std::move( source_file ) ) );
        }

        static JsonIn from( std::string source_file ) noexcept( false ) {
            std::shared_ptr<FlexBuffer> buffer = FlexBufferCache::global_cache().parse_and_cache( source_file );
            if( !buffer ) {
                throw JsonError( "Didn't get a flexbuffer but didn't get an exception either." );
            }
            return JsonIn( std::move( buffer ), make_shared_fast<std::string>( std::move( source_file ) ) );
        }

        JsonIn(std::istream& json) {
            json.seekg(0, std::istream::end);
            size_t len = json.tellg();
            std::string file_string;
            file_string.resize(len);
            json.seekg(0, std::istream::beg);
            json.read(&file_string[0], len);
            char buf[ 4096 ];
            while ( !json.eof() ) {
                json.read(buf, 4096);
                file_string.append(buf, json.gcount());
            }
            root_ = FlexBufferCache::global_cache().parse_buffer(file_string);
            parent_ = *root_;
            source_file_ = make_shared_fast<std::string>("");
            values_in_parent_ = 0;
        }

        JsonIn( std::shared_ptr<FlexBuffer> root, shared_ptr_fast<std::string> source_file ) : root_{ std::move( root ) },
            parent_{ *root_ }, source_file_{ std::move( source_file ) }, values_in_parent_{ 0 } {
        }

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

        JsonIn& seek( JsonPath const &path ) {
            path_ = path;
            parent_ = *root_;
            values_in_parent_ = 0;
            for( uint16_t idx : path_ ) {
                parent_ = parent_.AsVector()[ idx ];
                values_in_parent_ = count_values_in_parent();
            }
            return *this;
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
            if( test_string() ) {
                advance();
            } else {
                error( "Tried to skip a string, but it was a " + get_current_type() );
            }
        }
        void skip_value() {
            if( path_.last() < values_in_parent_ ) {
                advance();
            } else {
                error( "Tried to skip any value but there was none to skip." );
            }
        }
        void skip_object() {
            if( test_object() ) {
                advance();
            } else {
                error( "Tried to skip an object, but it was a " + get_current_type() );
            };
        }
        void skip_array() {
            if( test_array() ) {
                advance();
            } else {
                error( "Tried to skip an array, but it was a " + get_current_type() );
            };
        }
        void skip_true() {
            auto current = get_current();
            if( current.IsBool() && current.AsBool() == true ) {
                advance();
            } else {
                error( "Tried to skip a true, but it was a " + get_current_type() );
            }
        }
        void skip_false() {
            auto current = get_current();
            if( current.IsBool() && current.AsBool() == false ) {
                advance();
            } else {
                error( "Tried to skip a false, but it was a " + get_current_type() );
            };
        }
        void skip_null() {
            if( test_null() ) {
                advance();
            } else {
                error( "Tried to skip a null, but it was a " + get_current_type() );
            }
        }
        void skip_number() {
            if( test_number() ) {
                advance();
            } else {
                error( "Tried to skip a number, but it was a " + get_current_type() );
            };
        }

        size_t count_values_in_parent() {
            return parent_.AsVector().size();
        }

        flexbuffers::Reference get_current() {
            if( path_.size() > 0 ) {
                return parent_.AsVector()[path_.last()];
            }
            return *root_;
        }

        std::string const &get_current_type() {
            return flexbuffer_type_to_string( get_current().GetType() );
        }

        void rewind() {
            if( path_.size() == 0 ) {
                throw std::runtime_error( "Cannot rewind root array/object." );
            }
            if( path_.last() == 0 ) {
                throw std::runtime_error( "Cannot rewind past beginning of array/object." );
            }
            --path_.last();
        }

        void advance() {
            if( path_.size() == 0 ) {
                // Happens when getting root as array/object.
                return;
            }
            if( path_.last() >= count_values_in_parent() ) {
                throw std::runtime_error( "Cannot advance past end of array/object." );
            }
            ++path_.last();
        }

        void enter_iterable() {
            if( path_.size() > 0 ) {
                parent_ = parent_.AsVector()[ path_.last() ];
            }
            values_in_parent_ = count_values_in_parent();
            path_.push_back( 0 );
        }

        void end_iterable() {
            path_.pop_back();
            parent_ = *root_;
            values_in_parent_ = 0;
            for( uint16_t idx : path_ ) {
                parent_ = parent_.AsVector()[ idx ];
                values_in_parent_ = count_values_in_parent();
            }
        }

        void check_idx_before_deref() {
            if( path_.size() > 0 ) {
                if( path_.last() >= values_in_parent_ ) {
                    throw std::runtime_error( "Out of values in array/object" );
                }
            }
        }

        // data parsing
        // get the next value as a string
        std::string get_string() {
            check_idx_before_deref();
            flexbuffers::Reference value = get_current();
            if( value.IsString() ) {
                advance();
                return value.AsString().str();
            }
            throw std::runtime_error( "Not a string." );
        }

        // get the next value as an int
        int get_int() {
            check_idx_before_deref();
            flexbuffers::Reference value = get_current();
            if( value.IsNumeric() ) {
                advance();
                return static_cast<int>( value.AsInt64() );
            }
            throw std::runtime_error( "Not a number." );
        }

        // get the next value as an unsigned int
        unsigned int get_uint() {
            check_idx_before_deref();
            flexbuffers::Reference value = get_current();
            if( value.IsNumeric() ) {
                // These are always stored as signed ints.
                int64_t signed_value = value.AsInt64();
                if( signed_value >= 0 ) {
                    advance();
                    return static_cast<unsigned int>( signed_value );
                }
                throw std::runtime_error( "Unsigned value was negative" );
            }
            throw std::runtime_error( "Not a number." );
        }

        // get the next value as an int64
        int64_t get_int64() {
            check_idx_before_deref();
            flexbuffers::Reference value = get_current();
            if( value.IsNumeric() ) {
                advance();
                return value.AsInt64();
            }
            throw std::runtime_error( "Not a number." );
        }

        // get the next value as a uint64
        uint64_t get_uint64() {
            check_idx_before_deref();
            flexbuffers::Reference value = get_current();
            if( value.IsNumeric() ) {
                // These are always stored as signed ints.
                int64_t signed_value = value.AsInt64();
                if( signed_value >= 0 ) {
                    advance();
                    return static_cast<uint64_t>( signed_value );
                }
                throw std::runtime_error( "Unsigned value was negative" );
            }
            throw std::runtime_error( "Not a number." );
        }

        // get the next value as a bool
        bool get_bool() {
            check_idx_before_deref();
            flexbuffers::Reference value = get_current();
            if( value.IsBool() ) {
                advance();
                return value.AsBool();
            }
            throw std::runtime_error( "Not a bool." );
        }

        // get the next value as a double
        double get_float() {
            check_idx_before_deref();
            flexbuffers::Reference value = get_current();
            if( value.IsNumeric() ) {
                advance();
                return value.AsFloat();
            }
            throw std::runtime_error( "Not a number." );
        }

        // also strips the ':'
        std::string get_member_name() {
            check_idx_before_deref();
            if( parent_.IsMap() ) {
                return parent_.AsMap().Keys()[ path_.last() ].AsString().str();
            }
            throw std::runtime_error( "Not in an object" );
        }

        inline JsonValue get_value();
        inline JsonObject get_object();
        inline JsonArray get_array();

        template<typename E, typename = typename std::enable_if<std::is_enum<E>::value>::type>
        E get_enum_value() {
            try {
                return io::string_to_enum<E>( get_string() );
            } catch( const io::InvalidEnumString & ) {
                --path_.last();
                error( "invalid enumeration value" );
            }
        }

        // container control and iteration
        // verify array start
        void start_array() {
            flexbuffers::Reference current = get_current();
            if( current.IsVector() && !current.IsMap() ) {
                enter_iterable();
            }
            throw std::runtime_error( "Expected an array, got a " + get_current_type() );
        }
        // returns false if it's not the end
        bool end_array() {
            return path_.last() == values_in_parent_;
        }
        void start_object() {
            flexbuffers::Reference current = get_current();
            if( current.IsMap() ) {
                enter_iterable();
            }
            throw std::runtime_error( "Expected an object, got a " + get_current_type() );
        }
        // returns false if it's not the end
        bool end_object() {
            return path_.last() == values_in_parent_;
        }

        // type testing
        // These are slightly stricter than the text json implementations in that they require the iterator to be
        // at a valid value position.
        bool test_null() {
            return get_current().IsNull();
        }
        bool test_bool() {
            return get_current().IsBool();
        }
        bool test_number() {
            return get_current().IsNumeric();
        }
        bool test_int() {
            return test_number();
        }
        bool test_float() {
            return test_number();
        }
        bool test_string() {
            return get_current().IsString();
        }
        bool test_bitset() {
            // text json assumes a string
            return test_string();
        }
        bool test_array() {
            auto current = get_current();
            return current.IsVector() && !current.IsMap();
        }
        bool test_object() {
            return get_current().IsMap();
        }

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
        [[noreturn]] void error( const std::string &message, int offset = 0 ); // ditto
        // if the next element is a string, throw error after the `offset`th unicode
        // character in the parsed string. if `offset` is 0, throw error right after
        // the starting quotation mark.
        [[noreturn]] void string_error( const std::string &message, int offset );

        // If throw_, then call error( message, offset ), otherwise return
        // false
        bool error_or_false( bool do_throw, const std::string &message, int offset = 0 ) {
            if( do_throw ) {
                error( message, offset );
            }
            return false;
        }

        //std::string substr( size_t pos, size_t len = std::string::npos );

        static inline std::string const &flexbuffer_type_to_string( flexbuffers::Type t );
};

class Json
{
    public:
        Json() = delete;
        using FlexBuffer = FlexBufferCache::FlexBuffer;

        std::string str() const;
        [[noreturn]] void throw_error( std::string const &message, int offset = 0 ) const;
        [[noreturn]] void string_error(std::string const& message, int offset = 0) const;

    protected:
        Json( FlexBuffer &&json, std::string &&source_file ) : json_{ std::move( json ) },
            source_file_{ std::move( source_file ) } {}
        Json( FlexBuffer &&json, std::string &&source_file,
              JsonPath &&path ) : json_{ std::move( json ) },
            source_file_{ std::move( source_file ) },
            path_{ std::move( path ) } {}

        virtual ~Json() = default;

        // JsonIn keeps the backing memory alive, and other things besides, like the root reference.
        JsonIn *jsin_;
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

// A single value wrapper. Convertible to any actual value or array/object..
class JsonValue : Json
{
    public:
        JsonValue( FlexBuffer &&json, std::string source_file ) : Json( std::move( json ),
                    std::move( source_file ) ) {}
        JsonValue( FlexBuffer &&json, std::string source_file,
                   JsonPath &&path ) : Json( std::move( json ), std::move( source_file ),
                                                 std::move( path ) ) {}

        // NOLINTNEXTLINE(google-explicit-constructor)
        inline operator std::string() const;
        // NOLINTNEXTLINE(google-explicit-constructor)
        inline operator int() const;
        // NOLINTNEXTLINE(google-explicit-constructor)
        inline operator bool() const;
        // NOLINTNEXTLINE(google-explicit-constructor)
        inline operator double() const;
        // NOLINTNEXTLINE(google-explicit-constructor)
        inline operator JsonObject() const;
        // NOLINTNEXTLINE(google-explicit-constructor)
        inline operator JsonArray() const;

        template<typename T>
        bool read( T &t, bool throw_on_error = false ) const {
            return jsin_->seek(path_).read(t, throw_on_error);
        }

        bool test_string() const {
            return json_.IsString();
        }
        bool test_int() const {
            return json_.IsNumeric();
        }
        bool test_bool() const {
            return json_.IsBool();
        }
        bool test_float() const {
            return json_.IsNumeric();
        }
        bool test_object() const {
            return json_.IsMap();
        }
        bool test_array() const {
            return json_.IsVector() && !json_.IsMap();
        }
        bool test_null() const {
            return json_.IsNull();
        }

        std::string get_string() const {
            return (std::string)( *this );
        }
        int get_int() const {
            return (int)(*this);
        }
        double get_float() const {
            return (double)(*this);
        }
        JsonObject get_object() const;
        JsonArray get_array() const;


        using Json::throw_error;
        using Json::string_error;
        using Json::path_;
};

class JsonMember : public JsonValue
{
    public:
        JsonMember( flexbuffers::String name,
                    JsonValue value ) : JsonValue( std::move( value ) ),
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

class JsonObject : Json
{
    public:
        using FlexBuffer = FlexBufferCache::FlexBuffer;

    protected:
        flexbuffers::TypedVector keys_ = flexbuffers::TypedVector::EmptyTypedVector();
        flexbuffers::Vector values_ = flexbuffers::Vector::EmptyVector();
        mutable TinyBitSet visited_fields_bitset_;

    public:
        JsonObject(
            FlexBuffer &&json,
            std::string source_file )
            : Json( std::move( json ), std::move( source_file ) ) {
            init( json_ );
        }

        JsonObject(
            FlexBuffer &&json,
            std::string source_file,
            JsonPath &&path )
            : Json( std::move( json ), std::move( source_file )
            , std::move( path ) ) {
            init( json_ );
        }

        JsonObject &operator=( JsonObject const & ) = default;

    private:
        void init( FlexBuffer const &json ) {
            auto json_map = json.AsMap();
            keys_ = json_map.Keys();
            values_ = json_map.Values();
            visited_fields_bitset_.resize( keys_.size() );
        }
    public:
        size_t size() const {
            return keys_.size();
        }

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

        double get_float( std::string const &key ) const {
            return get_member( key.c_str() );
        }
        double get_float( const char *key ) const {
            return get_member( key );
        }

        bool get_bool( std::string const &key ) const {
            return get_member( key.c_str() );
        }
        bool get_bool( const char *key ) const {
            return get_member( key );
        }

        inline JsonArray get_array( std::string const &key ) const;
        inline JsonArray get_array( const char *key ) const;

        JsonObject get_object( std::string const &key ) const {
            return get_member( key.c_str() );
        }

        JsonObject get_object( const char *key ) const {
            return get_member( key );
        }

        /*template<typename E, typename = typename std::enable_if<std::is_enum<E>::value>::type>
        E get_enum_value(const std::string& name, const E fallback) const {
            if( !has_member(name) ) {
                return fallback;
            }
            mark_visited(name);
            jsin->seek(verify_position(name));
            return jsin->get_enum_value<E>();
        }*/

        template<typename E, typename = typename std::enable_if<std::is_enum<E>::value>::type>
        E get_enum_value(const std::string& name) const {
            return get_enum_value<E>(name.c_str());
        }
        template<typename E, typename = typename std::enable_if<std::is_enum<E>::value>::type>
        E get_enum_value(const char* name) const {
            auto value = get_member(name);
            try {
                return io::string_to_enum<E>((std::string)value);
            } catch( const io::InvalidEnumString& ) {
                value.throw_error("invalud enumumeration value");
            }
        }

        template<typename E, typename = typename std::enable_if<std::is_enum<E>::value>::type>
        E get_enum_value(const std::string& name, E fallback) const {
            return get_enum_value<E>(name.c_str(), fallback);
        }
        template<typename E, typename = typename std::enable_if<std::is_enum<E>::value>::type>
        E get_enum_value(const char* name, E fallback) const {
            auto value = get_member_opt(name);
            if( value.has_value() ) {
                try {
                    return io::string_to_enum<E>((std::string)*value);
                } catch( const io::InvalidEnumString& ) {
                    value->throw_error("invalud enumumeration value");
                }
            } else {
                return fallback;
            }
        }

        // Sigh.
        std::vector<int> get_int_array(const std::string& name) const;
        std::vector<std::string> get_string_array(const std::string& name) const;

        bool has_member( std::string const &key ) const {
            return has_member( key.c_str() );
        }

        bool has_member( const char *key ) const {
            size_t idx;
            return find_map_key_idx( key, keys_, idx );
        }

        bool has_null( const char *key ) const {
            auto ref = find_value_ref( key );
            return ref.IsNull();
        }
        bool has_null( const std::string &key ) const {
            return has_null( key.c_str() );
        }

        bool has_int( const char *key ) const {
            return has_number( key );
        }
        bool has_int( const std::string &key ) const {
            return has_number( key );
        }

        bool has_float(const char* key) const {
            return has_number(key);
        }
        bool has_float(const std::string& key) const {
            return has_number(key);
        }

        bool has_number( const char *key ) const {
            auto ref = find_value_ref( key );
            return ref.IsNumeric();
        }
        bool has_number( const std::string &key ) const {
            return has_number( key.c_str() );
        }

        bool has_string( std::string const &key ) const {
            return has_string( key.c_str() );
        }

        bool has_string( const char *key ) const {
            auto ref = find_value_ref( key );
            return ref.IsString();
        }

        bool has_bool(const std::string& key) const {
            return has_bool(key.c_str());
        }
        bool has_bool(const char* key) const {
            auto ref = find_value_ref(key);
            return ref.IsBool();
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
        bool has_object( const std::string &key ) const {
            return has_object( key.c_str() );
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

        double get_float(std::string const& key, double fallback) const {
            return get_float(key.c_str(), fallback);
        }
        double get_float(const char* key, double fallback) const {
            auto member_opt = get_member_opt(key);
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
        cata::optional<JsonValue> get_member_opt( const char *key ) const {
            size_t idx = 0;
            bool found = find_map_key_idx( key, keys_, idx );
            if( found ) {
                mark_visited( idx );
                return JsonValue{ values_[idx], std::string( source_file_ ), path_ + idx };
            }
            return cata::nullopt;
        }

        JsonValue get_member(const std::string& key) const {
            return get_member(key.c_str());
        }
        JsonValue get_member( const char *key ) const {
            // Manually bsearch for the key idx to store in visited_fields_bitset_.
            // flexbuffers::Map::operator[] will probably be faster but won't give us the idx,
            // which we need to track visited fields.
            size_t idx = 0;
            bool found = find_map_key_idx( key, keys_, idx );
            if( found ) {
                mark_visited( idx );
                return JsonValue{ values_[idx], std::string( source_file_ ), path_ + idx };
            }
            error_no_member( key );
            return ( *this )[key];
        }

        JsonValue operator[]( const char *key ) const {
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

        template <typename T = std::string, typename Res = std::set<T>>
        Res get_tags(const std::string& name) const {
            return get_tags<T, Res>(name.c_str());
        }

        template <typename T = std::string, typename Res = std::set<T>>
        Res get_tags( const char* name ) const {
            Res res;
            cata::optional<JsonValue> member_opt = get_member_opt( name );
            if( !member_opt.has_value() ) {
                return res;
            }

            JsonValue &member = *member_opt;

            // allow single string as tag
            if( member.test_string() ) {
                res.insert( T{ ( std::string )member } );
                return res;
            }

            // otherwise assume it's an array and error if it isn't.
            for( const std::string line : ( JsonArray )member ) {
                res.insert( T( line ) );
            }

            return res;
        }

        using Json::throw_error;
        [[noreturn]] void throw_error( const std::string &err, const std::string &member,
                                       int offset = 0 ) const {
            throw_error( err, member.c_str() );
        }
        [[noreturn]] void throw_error( const std::string &err, const char *member, int offset = 0 ) const {
            auto member_opt = get_member_opt( member );
            if( member_opt.has_value() ) {
                ( *member_opt ).throw_error( err, offset );
            }
        }

        void allow_omitted_members() const {
            visited_fields_bitset_.set_all();
        }

        void copy_visited_members(const JsonObject& rhs) const {
            visited_fields_bitset_ = rhs.visited_fields_bitset_;
        }

        json_source_location get_source_location() const;

        using Json::str;

    public:
        class const_iterator
        {
                const_iterator( JsonObject const &object, size_t pos )
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

                JsonMember operator*() const {
                    object_.mark_visited( pos_ );
                    return JsonMember(
                               object_.keys_[pos_].AsString(),
                               object_[pos_] );
                }

            private:
                JsonObject const &object_;
                size_t pos_;

                friend JsonObject;
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
        JsonValue operator[]( size_t idx ) const {
            mark_visited( idx );
            const char *key = keys_[idx].AsKey();
            return JsonValue{ values_[idx], std::string( source_file_ ), path_ + idx };
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

// An iterable wrapper around Json that always iterates values only.
class JsonArray : Json
{
    public:
        JsonArray(
            FlexBuffer &&json,
            std::string source_file )
            : Json( std::move( json ), std::move( source_file ) ) {
            init( json_ );
        }

        JsonArray(
            FlexBuffer &&json,
            std::string source_file,
            JsonPath &&path )
            : Json( std::move( json ), std::move( source_file )
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
                for( JsonValue value : *this ) {
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
                const_iterator( JsonArray const &object, size_t pos )
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

                JsonValue operator*() const {
                    array_.mark_visited( pos_ );

                    return JsonValue( array_[ pos_ ] );
                }

            private:
                JsonArray const &array_;
                size_t pos_;

                friend JsonArray;
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

        bool empty() const {
            return size() == 0;
        }

        JsonValue operator[](size_t idx) const {
            // Manually bsearch for the key idx to store in visited_fields_bitset_.
            // flexbuffers::Map::operator[] will probably be faster but won't give us the idx,
            // which we need to track visited fields.

            if( idx < size_ ) {
                mark_visited(idx);
                flexbuffers::Reference value;
                if( json_.IsFixedTypedVector() ) {
                    value = json_.AsFixedTypedVector()[ idx ];
                } else if( json_.IsTypedVector() ) {
                    value = json_.AsTypedVector()[ idx ];
                } else {
                    value = json_.AsVector()[ idx ];
                }
                return JsonValue{ std::move(value), std::string(source_file_), path_ + idx };
            }
            throw_error(std::to_string(idx) + " index is out of bounds.");
        }

        std::string get_string(size_t idx) const {
            return ( *this )[ idx ];
        }

        JsonArray get_array(size_t idx) const {
            return ( *this )[ idx ];
        }

        double get_float(size_t idx) const {
            return ( *this )[ idx ];
        }

        bool has_string(size_t idx) const {
            return ( *this )[ idx ].test_string();
        }

        bool has_int(size_t idx) const {
            return (*this)[idx].test_int();
        }

        bool has_array(size_t idx) const {
            return ( *this )[ idx ].test_array();
        }

        bool has_object(size_t idx) const {
            return (*this)[idx].test_object();
        }

        double next_float() {
            return get_next();
        }

        bool test_string() {
            return has_string(next_);
        }
        std::string next_string() {
            return get_next();
        }

        bool test_int() {
            return has_int(next_);
        }
        int next_int() {
            return get_next();
        }

        bool test_array() {
            return has_array(next_);
        }
        JsonArray next_array() {
            return get_next();
        }

        bool test_object() {
            return has_object(next_);
        }
        JsonObject next_object() {
            return get_next();
        }

        template<typename E, typename = typename std::enable_if<std::is_enum<E>::value>::type>
        E next_enum_value() {
            try {
                return io::string_to_enum<E>(next_string());
            } catch( const io::InvalidEnumString& ) {
                --path_.last();
                throw_error("invalid enumeration value");
            }
        }

        // random-access read values by reference
        template <typename T> bool read_next(T& t) {
            jsin_->seek(get_next().path_);
            return jsin_->read(t);
        }

        // random-access read values by reference
        template <typename T> bool read(size_t i, T& t, bool throw_on_error = false) const {
            jsin_->seek((*this)[i].path_);
            return jsin_->read(t, throw_on_error);
        }
        using Json::throw_error;

        [[noreturn]] void string_error(const std::string& message, size_t idx, int offset) const {
            ( *this )[ idx ].string_error(message, offset);
        }

        bool has_more() const {
            return next_ < size_;
        }

        template <typename T = std::string, typename Res = std::set<T>>
        Res get_tags(const size_t idx) const
        {
            Res res;

            JsonValue jv = ( *this )[ idx ];

            // allow single string as tag
            if( jv.test_string() ) {
                res.insert(T(jv.get_string()));
                return res;
            }

            for( const std::string line : jv.get_array() ) {
                res.insert(T(line));
            }

            return res;
        }

    private:

        JsonValue get_next() {
            return ( *this )[ next_++ ];
        }

        size_t size_;
        size_t next_ = 0;

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

        mutable TinyBitSet visited_fields_bitset_;

        void mark_visited( size_t idx ) const {
            visited_fields_bitset_.set( idx );
        }
};

template<typename T>
void deserialize(cata::optional<T>& obj, JsonIn& jsin)
{
    if( jsin.test_null() ) {
        obj.reset();
    } else {
        obj.emplace();
        jsin.read(*obj, true);
    }
}

#include "flexbuffer_json-inl.h"

#endif // CATA_SRC_FLEXBUFFER_JSON_H
