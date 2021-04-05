#pragma once
#ifndef CATA_SRC_FLEXBUFFER_JSON_H
#define CATA_SRC_FLEXBUFFER_JSON_H

#include <string>
#include <type_traits>

#include "flatbuffers/flexbuffers.h"

#include "cata_bitset.h"
#include "cata_small_literal_vector.h"
#include "flexbuffer_cache.h"
#include "text_json.h"
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

constexpr auto sizeit = sizeof( JsonPath );

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
                throw TextJsonError( "Didn't get a flexbuffer but didn't get an exception either." );
            }
            return FlexJsonValue( std::move( *buffer ), std::move( source_file ) );
        }

        FlexJsonValue( FlexBuffer &&json, std::string &&source_file ) : FlexJson( std::move( json ),
                    std::move( source_file ) ) {}
        FlexJsonValue( FlexBuffer &&json, std::string &&source_file,
                       JsonPath &&path ) : FlexJson( std::move( json ), std::move( source_file ), std::move( path ) ) {}


        // NOLINTNEXTLINE(google-explicit-constructor)
        inline operator const char*() const;
        // NOLINTNEXTLINE(google-explicit-constructor)
        //inline operator std::string() const;
        // NOLINTNEXTLINE(google-explicit-constructor)
        inline operator flexbuffers::String() const;
        // NOLINTNEXTLINE(google-explicit-constructor)
        inline operator int() const;
        // NOLINTNEXTLINE(google-explicit-constructor)
        inline operator bool() const;
        // NOLINTNEXTLINE(google-explicit-constructor)
        inline operator float() const;
        // NOLINTNEXTLINE(google-explicit-constructor)
        inline operator double() const;
        // NOLINTNEXTLINE(google-explicit-constructor)
        inline operator FlexJsonObject() const;
        // NOLINTNEXTLINE(google-explicit-constructor)
        inline operator FlexJsonArray() const;

        flexbuffers::String get_flex_string() const {
            if (json_.IsString()) {
                return json_.AsString();
            }
            throw_error("Expected a string, got a " + std::to_string(json_.GetType()));
        }

        bool is_null() const {
            return json_.IsNull();
        }

        bool test_null() const {
            return json_.IsNull();
        }

        bool test_array() const {
            return is_array();
        }

        bool is_array() const {
            return json_.IsAnyVector() && !json_.IsMap();
        }

        bool test_object() const {
            return is_object();
        }

        bool is_object() const {
            return json_.IsMap();
        }

        bool is_string() const {
            return json_.IsString();
        }

        std::string get_string() const {
            if (json_.IsString()) {
                return json_.AsString().str();
            }
            throw_error("Expected a string, got a " + std::to_string(json_.GetType()));
        }

        FlexJsonArray get_array() const;
        FlexJsonObject get_object() const;

        template<typename E, typename = typename std::enable_if<std::is_enum<E>::value>::type>
        E get_enum_value() {
            try {
                return io::string_to_enum<E>(get_string());
            } catch (const io::InvalidEnumString&) {
                error("invalid enumeration value");
            }
        }

        bool read(bool& b, bool throw_on_error = false);
        bool read(char& c, bool throw_on_error = false);
        bool read(signed char& c, bool throw_on_error = false);
        bool read(unsigned char& c, bool throw_on_error = false);
        bool read(short unsigned int& s, bool throw_on_error = false);
        bool read(short int& s, bool throw_on_error = false);
        bool read(int& i, bool throw_on_error = false);
        bool read(int64_t& i, bool throw_on_error = false);
        bool read(uint64_t& i, bool throw_on_error = false);
        bool read(unsigned int& u, bool throw_on_error = false);
        bool read(float& f, bool throw_on_error = false);
        bool read(double& d, bool throw_on_error = false);
        bool read(std::string& s, bool throw_on_error = false);
        template<size_t N>
        bool read(std::bitset<N>& b, bool throw_on_error = false);
        bool read(TextJsonDeserializer& j, bool throw_on_error = false);
        // This is for the string_id type
        template <typename T>
        auto read(T& thing, bool throw_on_error = false) -> decltype(thing.str(), true) {
            std::string tmp;
            if (!read(tmp, throw_on_error)) {
                return false;
            }
            thing = T(tmp);
            return true;
        }

        // This is for the int_id type
        template <typename T>
        auto read(int_id<T>& thing, bool throw_on_error = false) -> bool {
            std::string tmp;
            if (!read(tmp, throw_on_error)) {
                return false;
            }
            thing = int_id<T>(tmp);
            return true;
        }

        /// Overload that calls a global function `deserialize(T&,TextJsonIn&)`, if available.
        template<typename T>
        auto read(T& v, bool throw_on_error = false) ->
            decltype(deserialize(v, *this), true) {
            try {
                deserialize(v, *this);
                return true;
            } catch (const TextJsonError&) {
                if (throw_on_error) {
                    throw;
                }
                return false;
            }
        }

        /// Overload that calls a member function `T::deserialize(TextJsonIn&)`, if available.
        template<typename T>
        auto read(T& v, bool throw_on_error = false) -> decltype(v.deserialize(*this), true) {
            try {
                v.deserialize(*this);
                return true;
            } catch (const TextJsonError&) {
                if (throw_on_error) {
                    throw;
                }
                return false;
            }
        }

        template<typename T, std::enable_if_t<std::is_enum<T>::value, int> = 0>
        bool read(T& val, bool throw_on_error = false) {
            int i;
            if (read(i, false)) {
                val = static_cast<T>(i);
                return true;
            }
            std::string s;
            if (read(s, throw_on_error)) {
                val = io::string_to_enum<T>(s);
                return true;
            }
            return false;
        }

        /// Overload for std::pair
        template<typename T, typename U>
        bool read(std::pair<T, U>& p, bool throw_on_error = false) const;

        // array ~> array
        template <typename T, size_t N>
        bool read(std::array<T, N>& v, bool throw_on_error = false) const;

        // array ~> vector, deque, list
        template < typename T, typename std::enable_if <
            !std::is_same<void, typename T::value_type>::value >::type* = nullptr
        >
        auto read(T& v, bool throw_on_error = false) -> decltype(v.front(), true) const;

        // object ~> containers with matching key_type and value_type
        // set, unordered_set ~> object
        template <typename T, typename std::enable_if<
            std::is_same<typename T::key_type, typename T::value_type>::value>::type* = nullptr
        >
        bool read(T& v, bool throw_on_error = false) const;

        // special case for colony<item> as it supports RLE
        // see corresponding `write` for details
        template <typename T, std::enable_if_t<std::is_same<T, item>::value>* = nullptr >
        bool read(cata::colony<T>& v, bool throw_on_error = false) const;

        // special case for colony as it uses `insert()` instead of `push_back()`
        // and therefore doesn't fit with vector/deque/list
        // for colony of items there is another specialization with RLE
        template < typename T, std::enable_if_t < !std::is_same<T, item>::value >* = nullptr >
        bool read(cata::colony<T>& v, bool throw_on_error = false) const;

        // object ~> containers with unmatching key_type and value_type
        // map, unordered_map ~> object
        template < typename T, typename std::enable_if <
            !std::is_same<typename T::key_type, typename T::value_type>::value >::type* = nullptr
        >
        bool read(T& m, bool throw_on_error = true) const;

        using FlexJson::throw_error;

        void seek(std::make_signed_t<size_t>) {
            // lol
        }

        [[noreturn]] void error(const std::string& message, std::make_signed_t<size_t> offset = 0) {
            throw_error(message);
        }
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

        FlexJsonObject get_object() const {
            allow_omitted_members();
            return *this;
        }

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
            return get_member( key ).get_string();
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

        float get_float(std::string const& key) const {
            return get_member(key.c_str());
        }
        float get_float(const char* key) const {
            return get_member(key);
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

        bool has_int(const std::string& key) const {
            return has_number(key.c_str());
        }
        bool has_int(const char* key) const {
            return has_number(key);
        }

        bool has_float(const char* key) const {
            return has_number(key);
        }

        bool has_double(const char* key) const {
            return has_number(key);
        }

        bool has_string( std::string const &key ) const {
            return has_string( key.c_str() );
        }

        bool has_string( const char *key ) const {
            auto ref = find_value_ref( key );
            return ref.IsString();
        }

        bool has_null(const char* key) const {
            size_t idx = 0;
            bool found = find_map_key_idx(key, keys_, idx);
            if (found) {
                return values_[idx].IsNull();
            }
            return false;
        }

        bool has_array( std::string const &key ) const {
            return has_array( key.c_str() );
        }

        bool has_array( const char *key ) const {
            auto ref = find_value_ref( key );
            return ref.IsAnyVector() && !ref.IsMap();
        }

        bool has_object(std::string const& key) const {
            return has_object(key.c_str());
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

        double get_float(const std::string& key, double fallback) const {
            return get_float(key.c_str(), fallback);
        }
        double get_float(const char* key, double fallback) const {
            auto member_opt = get_member_opt(key);
            if (member_opt.has_value()) {
                return *member_opt;
            }
            return fallback;
        }

        std::vector<std::string> get_string_array(const std::string& name) const;

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

        FlexJsonValue get_member(std::string const& key) const {
            return get_member(key.c_str());
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

        template <typename T = std::string, typename Res = std::set<T>>
        Res get_tags(const std::string& name) const
        {
            Res res;
            auto member_opt = get_member_opt(name.c_str());
            if (member_opt.has_value()) {
                return res;
            }
            FlexJsonValue& member = member_opt.value();

            // allow single string as tag
            if (member.is_string()) {
                res.insert(T(member.get_string()));
                return res;
            }

            // otherwise assume it's an array and error if it isn't.
            for (flexbuffers::String line : (FlexJsonArray)member) {
                res.insert(T(line.str()));
            }

            return res;
        }

        template<typename E, typename = typename std::enable_if<std::is_enum<E>::value>::type>
        E get_enum_value(const std::string& name, const E fallback) const {
            auto member_opt = get_member_opt(name);
            if (member_opt.has_value()) {
                return fallback;
            }
            return (*member_opt).get_enum_value<E>();
        }
        template<typename E, typename = typename std::enable_if<std::is_enum<E>::value>::type>
        E get_enum_value(const std::string& name) const {
            return get_member(name).get_enum_value<E>();
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
            } catch( const TextJsonError & ) {
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
        std::string get_string(size_t idx) const {
            return (*this)[idx].get_string();
        }

        size_t size() const {
            return size_;
        }

        FlexJsonValue operator[](size_t idx) const {
            // Manually bsearch for the key idx to store in visited_fields_bitset_.
            // flexbuffers::Map::operator[] will probably be faster but won't give us the idx,
            // which we need to track visited fields.

            if (idx < size_) {
                mark_visited(idx);
                flexbuffers::Reference value;
                if (json_.IsFixedTypedVector()) {
                    value = json_.AsFixedTypedVector()[idx];
                } else if (json_.IsTypedVector()) {
                    value = json_.AsTypedVector()[idx];
                } else {
                    value = json_.AsVector()[idx];
                }
                return FlexJsonValue{ std::move(value), std::string(source_file_), path_ + idx };
            }
            throw_error(std::to_string(idx) + " index is out of bounds.");
        }

    private:
        size_t size_;

        void init(FlexBuffer const& json) {
            if (json.IsFixedTypedVector()) {
                auto json_vec = json.AsFixedTypedVector();
                size_ = json_vec.size();
            } else if (json.IsTypedVector()) {
                auto json_vec = json.AsTypedVector();
                size_ = json_vec.size();
            } else {
                auto json_vec = json.AsVector();
                size_ = json_vec.size();
            }
            visited_fields_bitset_.resize(size_);
        }

        mutable TinyBitSet visited_fields_bitset_;

        void mark_visited( size_t idx ) const {
            visited_fields_bitset_.set( idx );
        }
};

template<typename T>
void deserialize(cata::optional<T>& obj, FlexJsonValue jv)
{
    if (jv.test_null()) {
        obj.reset();
    } else {
        obj.emplace();
        jv.read(*obj, true);
    }
}

class FlexJsonDeserializer
{
public:
    virtual ~FlexJsonDeserializer() = default;
    virtual void deserialize(FlexJsonValue jsin) = 0;
    FlexJsonDeserializer() = default;
    FlexJsonDeserializer(FlexJsonDeserializer&&) = default;
    FlexJsonDeserializer(const FlexJsonDeserializer&) = default;
    FlexJsonDeserializer& operator=(FlexJsonDeserializer&&) = default;
    FlexJsonDeserializer& operator=(const FlexJsonDeserializer&) = default;
};

#include "flexbuffer_json-inl.h"

#endif // CATA_SRC_FLEXBUFFER_JSON_H
