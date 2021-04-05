#pragma once
#ifndef CATA_SRC_FLEXBUFFER_JSON_INL_H
#define CATA_SRC_FLEXBUFFER_JSON_INL_H

inline FlexJsonArray FlexJsonValue::get_array() const {
    return *this;
}

inline FlexJsonObject FlexJsonValue::get_object() const {
    return *this;
}

inline std::vector<std::string> FlexJsonObject::get_string_array(const std::string& name) const {
    std::vector<std::string> ret;
    for (FlexJsonValue entry : get_array(name)) {
        ret.push_back(entry.get_string());
    }
    return ret;
}

inline FlexJsonValue::operator const char*() const
{
    if (json_.IsString()) {
        return json_.AsString().c_str();
    }
    throw_error("Expected a string, got a " + std::to_string(json_.GetType()));
}
/*
inline FlexJsonValue::operator std::string() const
{
}
*/
inline FlexJsonValue::operator flexbuffers::String() const
{
    if (json_.IsString()) {
        return json_.AsString();
    }
    throw_error("Expected a string, got a " + std::to_string(json_.GetType()));
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

inline FlexJsonValue::operator float() const
{
    if( json_.IsNumeric() ) {
        return json_.AsFloat();
    }
    throw_error( "Expected a float, got a " + std::to_string( json_.GetType() ) );
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


/// Overload for std::pair
template<typename T, typename U>
inline bool FlexJsonValue::read(std::pair<T, U>& p, bool throw_on_error) const {
    if (!test_array()) {
        return error_or_false(throw_on_error, "Expected json array encoding pair");
    }
    try {
        FlexJsonArray ja = *this;
        if (ja.size() != 2) {
            return error_or_false(throw_on_error, "Array had wrong number of elements for pair");
        }
        bool result =
            ja[0].read(p.first, throw_on_error) &&
            ja[1].read(p.second, throw_on_error);
        if (!result) {
            return error_or_false(throw_on_error, "Failed to read pair.");
        }
        return result;
    } catch (const TextJsonError&) {
        if (throw_on_error) {
            throw;
        }
        return false;
    }
}

// array ~> array
template <typename T, size_t N>
inline bool FlexJsonValue::read(std::array<T, N>& v, bool throw_on_error) const {
    if (!test_array()) {
        return error_or_false(throw_on_error, "Expected json array");
    }
    try {
        FlexJsonArray ja = *this;
        for (size_t i = 0; i < std::max(ja.size(), N); ++i) {
            if (!ja[i].read(v[i], throw_on_error)) {
                return false; // invalid entry
            }
        }
        if (ja.size() < N) {
            return error_or_false(throw_on_error, "json array is too short");
        }
        if (ja.size() > N) {
            return error_or_false(throw_on_error, "Array had too many elements");
        }
        return true;
    } catch (const TextJsonError&) {
        if (throw_on_error) {
            throw;
        }
        return false;
    }
}

// array ~> vector, deque, list
template < typename T, typename std::enable_if <
    !std::is_same<void, typename T::value_type>::value >::type*
>
inline auto FlexJsonValue::read(T& v, bool throw_on_error) -> decltype(v.front(), true) const {
    if (!test_array()) {
        return error_or_false(throw_on_error, "Expected json array");
    }
    try {
        v.clear();
        for (FlexJsonValue jv : (FlexJsonArray)*this) {
            typename T::value_type element;
            if (jv.read(element, throw_on_error)) {
                v.push_back(std::move(element));
            }
        }
    } catch (const TextJsonError&) {
        if (throw_on_error) {
            throw;
        }
        return false;
    }

    return true;
}

// object ~> containers with matching key_type and value_type
// set, unordered_set ~> object
template <typename T, typename std::enable_if<
    std::is_same<typename T::key_type, typename T::value_type>::value>::type*>
inline bool FlexJsonValue::read(T& v, bool throw_on_error) const {
    if (!test_array()) {
        return error_or_false(throw_on_error, "Expected json array");
    }
    try {
        v.clear();
        for (FlexJsonValue jv : (FlexJsonArray)*this) {
            typename T::value_type element;
            if (jv.read(element, throw_on_error)) {
                v.insert(std::move(element));
            }
        }
    } catch (const TextJsonError&) {
        if (throw_on_error) {
            throw;
        }
        return false;
    }

    return true;
}

// special case for colony<item> as it supports RLE
// see corresponding `write` for details
template <typename T, std::enable_if_t<std::is_same<T, item>::value>*>
inline bool FlexJsonValue::read(cata::colony<T>& v, bool throw_on_error) const {
    if (!test_array()) {
        return error_or_false(throw_on_error, "Expected json array");
    }
    try {
        v.clear();
        for (FlexJsonValue jv : (FlexJsonArray)*this) {
            T element;
            if (jv.is_array()) {
                FlexJsonArray ja = jv;
                if (ja.size() != 2) {
                    error_or_false(throw_on_error, "Expected two elements for run length encoded array.");
                    continue;
                }
                int run_l;
                if (ja[0].read(element, throw_on_error) &&
                    ja[1].read(run_l, throw_on_error)
                    ) { // all is good
                      // first insert (run_l-1) elements
                    for (int i = 0; i < run_l - 1; i++) {
                        v.insert(element);
                    }
                    // micro-optimization, move last element
                    v.insert(std::move(element));
                } else { // array is malformed, skipping it entirely
                    error_or_false(throw_on_error, "Malformed run length encoding in array");
                }
            } else {
                if (jv.read(element, throw_on_error)) {
                    v.insert(std::move(element));
                }
            }
        }
    } catch (const TextJsonError&) {
        if (throw_on_error) {
            throw;
        }
        return false;
    }

    return true;
}

// special case for colony as it uses `insert()` instead of `push_back()`
// and therefore doesn't fit with vector/deque/list
// for colony of items there is another specialization with RLE
template < typename T, std::enable_if_t < !std::is_same<T, item>::value >*>
inline bool FlexJsonValue::read(cata::colony<T>& v, bool throw_on_error) const {
    if (!test_array()) {
        return error_or_false(throw_on_error, "Expected json array");
    }
    try {
        v.clear();
        for (FlexJsonValue jv : (FlexJsonArray)*this) {
            T element;
            if (jv.read(element, throw_on_error)) {
                v.insert(std::move(element));
            }
        }
    } catch (const TextJsonError&) {
        if (throw_on_error) {
            throw;
        }
        return false;
    }

    return true;
}

// object ~> containers with unmatching key_type and value_type
// map, unordered_map ~> object
template < typename T, typename std::enable_if <
    !std::is_same<typename T::key_type, typename T::value_type>::value >::type*
>
inline bool FlexJsonValue::read(T& m, bool throw_on_error) const {
    if (!test_object()) {
        return error_or_false(throw_on_error, "Expected json object");
    }
    try {
        m.clear();
        for (FlexJsonMember jm : (FlexJsonObject)*this) {
            using key_type = typename T::key_type;
            key_type key = key_from_json_string<key_type>{}(jm.name());
            typename T::mapped_type element;
            if (jm.read(element, throw_on_error)) {
                m[std::move(key)] = std::move(element);
            }
        }
    } catch (const TextJsonError&) {
        if (throw_on_error) {
            throw;
        }
        return false;
    }

    return true;
}

#endif // CATA_SRC_FLEXBUFFER_JSON_INL_H
