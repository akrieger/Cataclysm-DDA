#pragma once
#ifndef CATA_SRC_STRING_FORMATTER_H
#define CATA_SRC_STRING_FORMATTER_H

#include <cstddef>
#include <iosfwd>
#include <new>
#include <string>
#include <type_traits>
#include <typeinfo>

#include "demangle.h"
// TODO: replace with std::optional
#include "optional.h"

#include <fmt/format.h>

class translation;

namespace cata
{

template<typename T>
using is_translation = typename std::conditional <
    std::is_same<typename std::decay<T>::type, translation>::value, std::true_type,
    std::false_type >::type;

template<typename E, typename = std::enable_if_t<std::is_enum_v<E>>>
auto format_as(E e) { return fmt::underlying(e); }

/**@}*/

} // namespace cata

/**
 * Simple wrapper over @ref string_formatter::parse. It catches any exceptions and returns
 * some error string. Otherwise it just returns the formatted string.
 *
 * These functions perform string formatting according to the rules of the `printf` function,
 * see `man 3 printf` or any other documentation.
 *
 * In short: the \p format parameter is a string with optional placeholders, which will be
 * replaced with formatted data from the further arguments. The further arguments must have
 * a type that matches the type expected by the placeholder.
 * The placeholders look like this:
 * - `%s` expects an argument of type `const char*` or `std::string` or numeric (which is
 *   converted to a string via `to_string`), which is inserted as is.
 * - `%d` expects an argument of an integer type (int, short, ...), which is formatted as
 *   decimal number.
 * - `%f` expects a numeric argument (integer / floating point), which is formatted as
 *   decimal number.
 *
 * There are more placeholders and options to them (see documentation of `printf`).
 * Note that this wrapper (via @ref string_formatter) automatically converts the arguments
 * to match the given format specifier (if possible) - see @ref string_formatter_convert.
 */
/**@{*/

template<typename E, typename = std::enable_if_t<std::is_enum_v<E>>>
auto format_as(E e) { return fmt::underlying(e); }

template<typename ...Args>
inline std::string string_format( std::string format, Args &&...args )
{
    return fmt::format(format, std::forward<Args>(args)...);
}
template<typename ...Args>
inline std::string string_format( const char *format, Args &&...args )
{
    return fmt::format(format, std::forward<Args>(args)...);
}
template<typename T, typename ...Args>
inline typename std::enable_if<cata::is_translation<T>::value, std::string>::type
string_format( T &&format, Args &&...args )
{
    return fmt::format(format.translated(), std::forward<Args>(args)...);
}
/**@}*/

#endif // CATA_SRC_STRING_FORMATTER_H
