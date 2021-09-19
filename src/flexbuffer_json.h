#pragma once
#ifndef CATA_SRC_FLEXBUFFER_JSON_H
#define CATA_SRC_FLEXBUFFER_JSON_H

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

// Represents a 'path' in a json object, a series of object keys or indices, that when accessed from the root get you to some element in the json structure.
struct JsonPath {
        // Flexbuffer structures are either arrays or objects, where objects are just
        // arrays with an extra 'key vector'. As such, we can store a jsonpath as a series
        // of numeric indices and not store any string pointer values at all. This lets us
        // compress the datastructure to a series of shorts (at least one vector has >1k
        // elements in it). How this index should be interpreted depends on the type of
        // the json value at that location. Arrays as native indices, objects as an index
        // into the object's corresponding key vector.

        using index_type = uint16_t;

        JsonPath() = default;
        JsonPath( JsonPath const & ) = default;
        JsonPath( JsonPath && ) = default;

        JsonPath &operator=( JsonPath const & ) = default;
        JsonPath &operator=( JsonPath && ) = default;

        inline JsonPath &push_back( size_t idx ) {
            if( idx < std::numeric_limits<index_type>::max() ) {
                path_.push_back( static_cast<index_type>( idx ) );
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
        static constexpr size_t INLINE_PATH_SEGMENTS = 7;
        SmallLiteralVector<index_type, INLINE_PATH_SEGMENTS> path_;
};

class FlexJsonObject;
class FlexJsonArray;
class FlexJsonValue;
class FlexJsonMember;

class FlexJson
{
    public:

        FlexJson() = delete;
        using flexbuffer = flexbuffers::Reference;

        std::string str() const;
        [[noreturn]] void throw_error( JsonPath const &path, std::string const &message,
                                       int offset = 0 ) const;
        [[noreturn]] void throw_error_after( JsonPath const &path, std::string const &message ) const;
        [[noreturn]] void string_error( JsonPath const &path, std::string const &message,
                                        int offset = 0 ) const;

        static inline std::string const &flexbuffer_type_to_string( flexbuffers::Type t );

    protected:

        FlexJson( std::shared_ptr<parsed_flexbuffer> root, flexbuffer &&json ) : root_{ std::move( root ) },
            json_ { std::move( json ) } {}

        virtual ~FlexJson() = default;

        FlexJson( FlexJson const & ) = default;
        FlexJson &operator=( FlexJson const & ) = default;

        FlexJson( FlexJson && ) = default;
        FlexJson &operator=( FlexJson && ) = default;

        // Keeps the backing memory alive, also contains the root reference and source file for error logging.
        std::shared_ptr<parsed_flexbuffer> root_;
        // The actual thing we are pointing to with this Json instance.
        flexbuffer json_;

        static flexbuffer flexbuffer_root_from_storage( const std::shared_ptr<flexbuffer_storage>
                &storage ) {
            return flexbuffers::GetRoot( storage->data(), storage->size() );
        }
};

class FlexJsonWithPath : protected FlexJson
{
    public:
        [[noreturn]] void throw_error( const std::string &message,
                                       int offset = 0 ) const noexcept( false ) {
            FlexJson::throw_error( path_, message, offset );
        }
        [[noreturn]] void throw_error_after( const std::string &message ) const noexcept( false ) {
            FlexJson::throw_error_after( path_, message );
        }

    protected:

        FlexJsonWithPath( std::shared_ptr<parsed_flexbuffer> root, flexbuffer json,
                          JsonPath &&path ) : FlexJson( std::move( root ), std::move( json ) ), path_{ std::move( path ) } {}

        FlexJsonWithPath( FlexJsonWithPath const & ) = default;
        FlexJsonWithPath &operator=( FlexJsonWithPath const & ) = default;

        FlexJsonWithPath( FlexJsonWithPath && ) = default;
        FlexJsonWithPath &operator=( FlexJsonWithPath && ) = default;

        inline bool error_or_false( bool throw_on_error,
                                    std::string const &message ) const noexcept( false ) {
            if( throw_on_error ) {
                throw_error( message );
            }
            return false;
        }

        JsonPath path_;
};

// A single value wrapper. Convertible to any actual value or array/object.
// To keep creating these as trivial as possible, these do not contain a JsonPath. The typical
// pattern with JsonValue's is to immediately cast to the desired type, and generally are
// retrieved while iterating members of a JsonObject or values in a JsonArray. This
// means the parent_path will outlive the FlexJsonValue. But we have no real way to guarantee
// this without resorting to moving the JsonPath into a heap allocation with shared_ptr.
// We still aren't totally trivial because FlexJson has a shared_ptr member, but it's close.
class FlexJsonValue : FlexJson
{
    public:
        static cata::optional<FlexJsonValue> fromOpt( fs::path source_file,
                size_t offset = 0 ) noexcept( false ) {
            if( !file_exist( source_file ) ) {
                return cata::nullopt;
            }

            std::shared_ptr<parsed_flexbuffer> buffer = flexbuffer_cache::global_cache().parse_and_cache(
                        source_file, offset );
            if( !buffer ) {
                return cata::nullopt;
            }

            flexbuffer buffer_root = flexbuffer_root_from_storage( buffer->get_storage() );
            return FlexJsonValue( std::move( buffer ), std::move( buffer_root ), nullptr, 0 );
        }

        static FlexJsonValue from( fs::path source_file, size_t offset = 0 ) noexcept( false ) {
            if( !file_exist( source_file ) ) {
                throw JsonError( source_file.generic_u8string() + " does not exist." );
            }
            auto obj = fromOpt( source_file, offset );

            if( !obj ) {
                throw JsonError( "Json file " + source_file.generic_u8string() + " did not contain valid json" );
            }
            return std::move( *obj );
        }

        static FlexJsonValue fromString( std::string const &data ) noexcept( false ) {
            std::shared_ptr<parsed_flexbuffer> buffer = flexbuffer_cache::parse_buffer( data );
            if( !buffer ) {
                throw JsonError( "Failed to parse string into json" );
            }

            flexbuffer buffer_root = flexbuffer_root_from_storage( buffer->get_storage() );
            return FlexJsonValue( std::move( buffer ), std::move( buffer_root ), nullptr, 0 );
        }

        FlexJsonValue( std::shared_ptr<parsed_flexbuffer> root, flexbuffer json,
                       JsonPath const *parent_path, size_t path_index ) : FlexJson(
                               std::move( root ),
                               std::move( json ) ), parent_path_{ parent_path }, path_index_{ path_index } {}

        FlexJsonValue( FlexJsonValue const & ) = default;
        FlexJsonValue &operator=( FlexJsonValue const & ) = default;

        FlexJsonValue( FlexJsonValue && ) = default;
        FlexJsonValue &operator=( FlexJsonValue && ) = default;

        // NOLINTNEXTLINE(google-explicit-constructor)
        inline operator std::string() const;
        // NOLINTNEXTLINE(google-explicit-constructor)
        inline operator bool() const;
        // NOLINTNEXTLINE(google-explicit-constructor)
        inline operator int() const;
        // NOLINTNEXTLINE(google-explicit-constructor)
        inline operator unsigned() const;
        // NOLINTNEXTLINE(google-explicit-constructor)
        inline operator int64_t() const;
        // NOLINTNEXTLINE(google-explicit-constructor)
        inline operator uint64_t() const;
        // NOLINTNEXTLINE(google-explicit-constructor)
        inline operator double() const;
        // NOLINTNEXTLINE(google-explicit-constructor)
        inline operator FlexJsonObject() const;
        // NOLINTNEXTLINE(google-explicit-constructor)
        inline operator FlexJsonArray() const;

        bool test_string() const;
        bool test_bool() const;
        bool test_number() const;
        bool test_int() const;
        bool test_float() const;
        bool test_object() const;
        bool test_array() const;
        bool test_null() const;

        std::string get_string() const;
        bool get_bool() const;
        int get_int() const;
        unsigned int get_uint() const;
        int64_t get_int64() const;
        uint64_t get_uint64() const;
        double get_float() const;
        FlexJsonArray get_array() const;
        FlexJsonObject get_object() const;

        using FlexJson::throw_error;
        using FlexJson::throw_error_after;
        using FlexJson::string_error;

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

        // This is for the string_id type
        template <typename T>
        auto read( string_id<T> &thing, bool throw_on_error = false ) const -> bool;

        // This is for the int_id type
        template <typename T>
        auto read( int_id<T> &thing, bool throw_on_error = false ) const -> bool;

        /// Overload that calls a global function `deserialize(T&,JsonIn&)`, if available.
        template<typename T>
        auto read( T &v, bool throw_on_error = false ) const ->
        decltype( deserialize( v, *this ), true );

        /// Overload that calls a member function `T::deserialize(JsonIn&)`, if available.
        template<typename T>
        auto read( T &v, bool throw_on_error = false ) const -> decltype( v.deserialize( *this ), true );

        template<typename T, std::enable_if_t<std::is_enum<T>::value, int> = 0>
        bool read( T &val, bool throw_on_error = false ) const;

        /// Overload for std::pair
        template<typename T, typename U>
        bool read( std::pair<T, U> &p, bool throw_on_error = false ) const;

        // array ~> vector, deque, list
        template < typename T, typename std::enable_if <
                       !std::is_same<void, typename T::value_type>::value >::type * = nullptr
                   >
        auto read( T &v, bool throw_on_error = false ) const -> decltype( v.front(), true );

        // array ~> array
        template <typename T, size_t N>
        bool read( std::array<T, N> &v, bool throw_on_error = false ) const;

        // object ~> containers with matching key_type and value_type
        // set, unordered_set ~> object
        template <typename T, typename std::enable_if<
                      std::is_same<typename T::key_type, typename T::value_type>::value>::type * = nullptr
                  >
        bool read( T &v, bool throw_on_error = false ) const;

        // special case for colony<item> as it supports RLE
        // see corresponding `write` for details
        template <typename T, std::enable_if_t<std::is_same<T, item>::value>* = nullptr >
        bool read( cata::colony<T> &v, bool throw_on_error = false ) const;

        // special case for colony as it uses `insert()` instead of `push_back()`
        // and therefore doesn't fit with vector/deque/list
        // for colony of items there is another specialization with RLE
        template < typename T, std::enable_if_t < !std::is_same<T, item>::value > * = nullptr >
        bool read( cata::colony<T> &v, bool throw_on_error = false ) const;

        // object ~> containers with unmatching key_type and value_type
        // map, unordered_map ~> object
        template < typename T, typename std::enable_if <
                       !std::is_same<typename T::key_type, typename T::value_type>::value >::type * = nullptr
                   >
        bool read( T &m, bool throw_on_error = true ) const;

        [[noreturn]] void string_error( std::string const &message,
                                        int offset = 0 ) const noexcept( false ) {
            JsonPath p;
            if( parent_path_ ) {
                p = *parent_path_ + path_index_;
            }
            string_error( p, message, offset );
        }

        [[noreturn]] void throw_error( std::string const &message,
                                       int offset = 0 ) const noexcept( false ) {
            JsonPath p;
            if( parent_path_ ) {
                p = *parent_path_ + path_index_;
            }
            throw_error( p, message, offset );
        }

        [[noreturn]] void throw_error_after( std::string const &message ) const noexcept( false ) {
            JsonPath p;
            if( parent_path_ ) {
                p = *parent_path_ + path_index_;
            }
            throw_error_after( p, message );
        }
    private:
        JsonPath const *parent_path_;
        size_t path_index_;

        // Putting this in the header allows inlining and eliding a bunch of code with constant folding.
        bool error_or_false( bool throw_on_error, std::string const &message ) const noexcept( false ) {
            if( throw_on_error ) {
                throw_error( message );
            }
            return false;
        }
};

class FlexJsonArray : FlexJsonWithPath
{
        static const auto &empty_array_() {
            static auto empty_array = flexbuffer_cache::global_cache().parse_buffer( "[]" );
            return empty_array;
        }

    public:

        FlexJsonArray() : FlexJsonWithPath( empty_array_(),
                                                flexbuffer_root_from_storage( empty_array_()->get_storage() ), {} ) {}

        FlexJsonArray(
            std::shared_ptr<parsed_flexbuffer> root,
            flexbuffer &&json,
            JsonPath &&path )
            : FlexJsonWithPath( std::move( root ), std::move( json ), std::move( path ) ) {
            init( json_ );
        }

        FlexJsonArray( FlexJsonArray const &rhs ) : FlexJsonWithPath( rhs ) {
            init( json_ );
        }

        FlexJsonArray &operator=( FlexJsonArray const &rhs ) {
            FlexJsonWithPath::operator=( rhs );
            init( json_ );
            return *this;
        }

        FlexJsonArray( FlexJsonArray &&rhs ) noexcept :
            FlexJsonWithPath( std::move( static_cast<FlexJsonWithPath &>( rhs ) ) ) {
            init( json_, &rhs.visited_fields_bitset_ );
            rhs.visited_fields_bitset_.set_all();
        }

        FlexJsonArray &operator=( FlexJsonArray &&rhs ) noexcept {
            FlexJsonWithPath::operator=( rhs );
            init( json_, &rhs.visited_fields_bitset_ );
            rhs.visited_fields_bitset_.set_all();
            return *this;
        }

        class const_iterator;
        friend const_iterator;

        // Iterates the values in this array.
        const_iterator begin() const;
        const_iterator end() const;

        size_t size() const {
            return size_;
        }

        bool empty() const {
            return size() == 0;
        }

        FlexJsonValue operator[]( size_t idx ) const;

        std::string get_string( size_t idx ) const;
        int get_int( size_t idx ) const;
        double get_float( size_t idx ) const;
        FlexJsonArray get_array( size_t idx ) const;
        FlexJsonObject get_object( size_t idx ) const;

        bool has_string( size_t idx ) const;
        bool has_bool( size_t idx ) const;
        bool has_int( size_t idx ) const;
        bool has_float( size_t idx ) const;
        bool has_array( size_t idx ) const;
        bool has_object( size_t idx ) const;

        bool test_string() const;
        std::string next_string();

        bool test_bool() const;
        bool next_bool();

        bool test_int() const;
        int next_int();

        bool test_float() const;
        double next_float();

        bool test_array() const;
        FlexJsonArray next_array();

        bool test_object() const;
        FlexJsonObject next_object();

        FlexJsonValue next_value();

        template<typename E, typename = typename std::enable_if<std::is_enum<E>::value>::type>
        E next_enum_value() ;

        // array ~> vector, deque, list
        template < typename T, typename std::enable_if <
                       !std::is_same<void, typename T::value_type>::value >::type * = nullptr
                   >
        auto read( T &v, bool throw_on_error = false ) -> decltype( v.front(), true ) const;

        // random-access read values by reference
        template <typename T> bool read_next( T &t, bool throw_on_error = false );

        // random-access read values by reference
        template <typename T> bool read( size_t idx, T &t, bool throw_on_error = false ) const;

        template <typename T = std::string, typename Res = std::set<T>>
        Res get_tags( const size_t idx ) const;

        [[noreturn]] void string_error( const std::string &message, size_t idx, int offset ) const;

        bool has_more() const {
            return next_ < size_;
        }

        using FlexJsonWithPath::throw_error;
        using FlexJsonWithPath::throw_error_after;
        using FlexJsonWithPath::error_or_false;

    private:
        FlexJsonValue get_next();

        size_t size_ = 0;
        size_t next_ = 0;

        void init( flexbuffer const &json, TinyBitSet *moved_visited_fields = nullptr ) {
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
            if( moved_visited_fields ) {
                visited_fields_bitset_.swap( *moved_visited_fields );
            } else {
                visited_fields_bitset_.resize( size_ );
            }
        }

        mutable TinyBitSet visited_fields_bitset_;

        void mark_visited( size_t idx ) const {
            visited_fields_bitset_.set( idx );
        }
};

class FlexJsonMember : public FlexJsonValue
{
    public:
        FlexJsonMember( flexbuffers::String name,
                        FlexJsonValue value ) : FlexJsonValue( std::move( value ) ),
            name_( name ) { }

        FlexJsonMember( FlexJsonMember const & ) = default;
        FlexJsonMember &operator=( FlexJsonMember const & ) = default;

        FlexJsonMember( FlexJsonMember && ) = default;
        FlexJsonMember &operator=( FlexJsonMember && ) = default;

        std::string name() const {
            return name_.str();
        }

        bool is_comment() const {
            return strncmp( name_.c_str(), "//", 2 ) == 0;
        }

    private:
        flexbuffers::String name_;
};

class FlexJsonObject : FlexJsonWithPath
{
    protected:
        flexbuffers::TypedVector keys_ = flexbuffers::TypedVector::EmptyTypedVector();
        flexbuffers::Vector values_ = flexbuffers::Vector::EmptyVector();
        mutable TinyBitSet visited_fields_bitset_;

        static const auto &empty_object_() {
            static auto empty_object = flexbuffer_cache::parse_buffer( "{}" );
            return empty_object;
        }

    public:

        FlexJsonObject() : FlexJsonWithPath( empty_object_(),
                                                 flexbuffer_root_from_storage( empty_object_()->get_storage() ), {} ) {}

        FlexJsonObject(
            std::shared_ptr<parsed_flexbuffer> root,
            flexbuffer &&json,
            JsonPath &&path
        )
            : FlexJsonWithPath( std::move( root ), std::move( json ), std::move( path ) ) {
            init( json_ );
        }

        FlexJsonObject( FlexJsonObject const &rhs ) : FlexJsonWithPath( rhs ) {
            init( json_ );
        }

        FlexJsonObject &operator=( FlexJsonObject const &rhs ) {
            FlexJsonWithPath::operator=( rhs );
            init( json_ );
            // Copying an object resets visited fields.
            visited_fields_bitset_.clear_all();
            return *this;
        }

        FlexJsonObject( FlexJsonObject &&rhs ) noexcept :
            FlexJsonWithPath( std::move( static_cast<FlexJsonWithPath &>( rhs ) ) )  {
            init( json_, &rhs.visited_fields_bitset_ );
            rhs.visited_fields_bitset_.set_all();
        }

        FlexJsonObject &operator=( FlexJsonObject &&rhs ) noexcept {
            FlexJsonWithPath::operator=( std::move( static_cast<FlexJsonWithPath &>( rhs ) ) );
            init( json_, &rhs.visited_fields_bitset_ );
            rhs.visited_fields_bitset_.set_all();
            return *this;
        }

        ~JsonObject();

    private:
        void init( flexbuffer const &json, TinyBitSet *moved_visited_fields = nullptr ) {
            auto json_map = json.AsMap();
            keys_ = json_map.Keys();
            values_ = json_map.Values();
            if( moved_visited_fields ) {
                visited_fields_bitset_.swap( *moved_visited_fields );
            } else {
                visited_fields_bitset_.resize( keys_.size() );
            }
        }

    public:
        size_t size() const {
            return keys_.size();
        }

        std::string get_string( std::string const &key ) const;
        std::string get_string( const char *key ) const;

        template<typename T, typename std::enable_if_t<std::is_convertible<T, std::string>::value>* = nullptr>
        std::string get_string( std::string const &key, T && fallback ) const;

        template<typename T, typename std::enable_if_t<std::is_convertible<T, std::string>::value>* = nullptr>
        std::string get_string( const char *key, T && fallback ) const;

        // Vanilla accessors. Just return the named member and use it's conversion function.
        bool get_bool( std::string const &key ) const;
        bool get_bool( const char *key ) const;

        int get_int( std::string const &key ) const;
        int get_int( const char *key ) const;

        double get_float( std::string const &key ) const;
        double get_float( const char *key ) const;

        inline FlexJsonArray get_array( std::string const &key ) const;
        inline FlexJsonArray get_array( const char *key ) const;

        FlexJsonObject get_object( std::string const &key ) const;
        FlexJsonObject get_object( const char *key ) const;

        template<typename E, typename = typename std::enable_if<std::is_enum<E>::value>::type>
        E get_enum_value( const std::string &name ) const;
        template<typename E, typename = typename std::enable_if<std::is_enum<E>::value>::type>
        E get_enum_value( const char *name ) const;

        template<typename E, typename = typename std::enable_if<std::is_enum<E>::value>::type>
        E get_enum_value( const std::string &name, E fallback ) const;
        template<typename E, typename = typename std::enable_if<std::is_enum<E>::value>::type>
        E get_enum_value( const char *name, E fallback ) const;

        // Sigh.
        std::vector<int> get_int_array( const std::string &name ) const;
        std::vector<std::string> get_string_array( const std::string &name ) const;

        bool has_member( std::string const &key ) const;
        bool has_member( const char *key ) const;

        bool has_null( const char *key ) const;
        bool has_null( const std::string &key ) const;

        bool has_string( std::string const &key ) const;
        bool has_string( const char *key ) const;

        bool has_bool( const std::string &key ) const;
        bool has_bool( const char *key ) const;

        bool has_number( const char *key ) const;
        bool has_number( const std::string &key ) const;

        bool has_int( const char *key ) const;
        bool has_int( const std::string &key ) const;

        bool has_float( const char *key ) const;
        bool has_float( const std::string &key ) const;

        bool has_array( std::string const &key ) const;
        bool has_array( const char *key ) const;

        bool has_object( const char *key ) const;
        bool has_object( const std::string &key ) const;

        // Fallback accessors. Test if the named member exists, and if yes, return it,
        // else will return the fallback value. Does *not* test the member is the type
        // being requested.
        bool get_bool( std::string const &key, bool fallback ) const;
        bool get_bool( const char *key, bool fallback ) const;

        int get_int( std::string const &key, int fallback ) const;
        int get_int( const char *key, int fallback ) const;

        double get_float( std::string const &key, double fallback ) const;
        double get_float( const char *key, double fallback ) const;

        // Tries to get the member, and if found, calls it visited.
        cata::optional<FlexJsonValue> get_member_opt( const char *key ) const;
        FlexJsonValue get_member( const std::string &key ) const;
        FlexJsonValue get_member( const char *key ) const;
        FlexJsonValue operator[]( const char *key ) const;

        // Schwillions of read overloads
        template <typename T>
        bool read( const char *name, T &t, bool throw_on_error = true ) const;
        template <typename T>
        bool read( const std::string &name, T &t, bool throw_on_error = true ) const;

        template <typename T = std::string, typename Res = std::set<T>>
        Res get_tags( const std::string &name ) const;

        template <typename T = std::string, typename Res = std::set<T>>
        Res get_tags( const char *name ) const;

        [[noreturn]] void throw_error( const std::string &err, int offset = 0 ) const;

        [[noreturn]] void throw_error( const std::string &err, const std::string &member,
                                       int offset = 0 ) const;
        [[noreturn]] void throw_error( const std::string &err, const char *member, int offset = 0 ) const;

        void allow_omitted_members() const {
            visited_fields_bitset_.set_all();
        }

        void copy_visited_members( const FlexJsonObject &rhs ) const {
            visited_fields_bitset_ = rhs.visited_fields_bitset_;
        }

        using FlexJson::str;

    public:
        class const_iterator;
        friend const_iterator;

        const_iterator begin() const;
        const_iterator end() const;

    private:
        // Only called by the iterator which can't be manually constructed.
        FlexJsonValue operator[]( size_t idx ) const;

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

        void report_unvisited() const;

        // Reports an error via JsonObject at this location.
        [[noreturn]] void error_no_member( std::string const &member ) const;

        // debugmsg prints all the skipped members.
        void error_skipped_members( std::vector<size_t> const &skipped_members ) const;
};

// Having this uncommented causes unable to match definition errors for msvc.
// The implementation still exists in the -inl header but has to come after all
// the definitions for JsonValue::read().
//template<typename T>
//void deserialize( cata::optional<T> &obj, const FlexJsonValue &jsin );

void add_array_to_set( std::set<std::string> &s, const FlexJsonObject &json,
                       const std::string &name );

#include "flexbuffer_json-inl.h"

#endif // CATA_SRC_FLEXBUFFER_JSON_H
