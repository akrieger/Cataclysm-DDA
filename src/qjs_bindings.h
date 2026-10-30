#pragma once
#ifndef CATA_SRC_QJS_BINDINGS_H
#define CATA_SRC_QJS_BINDINGS_H

#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <vector>

#include <qjs/quickjs.h>

#include "cata_compiler_support.h"
#include "qjs.h"

template<typename T>
inline auto from_js( JSContext *ctx,
                     JSValueConst v ) -> std::enable_if_t<std::is_floating_point_v<T>, T>
{
    T ret{};
    // JS_IsNumber => int or float
    if( JS_IsNumber( v ) ) {
        double res = 0.0;
        // JS_ToFloat64 handles ints, floats, but not bigints. Sigh.
        int failed = JS_ToFloat64( ctx, &res, v );
        if( failed ) {
            // do still more evil
            throw std::runtime_error( "Failed to convert to int." );
        }
        ret = static_cast<T>( res );
    } else if( JS_IsBigInt( v ) ) {
        int64_t res = 0;
        int failed = JS_ToBigInt64( ctx, &res, v );
        if( failed ) {
            // do still more evil
            throw std::runtime_error( "Failed to convert to int." );
        }
        ret = static_cast<T>( res );
    }

    return ret;
}

template<typename T>
inline auto from_js( JSContext *ctx, JSValueConst v ) -> std::enable_if_t<std::is_integral_v<T>, T>
{
    // need to check convertibility and set a VM error
    if( !JS_IsNumber( v ) && !JS_IsBigInt( v ) ) {
        // do more different evil
        throw std::runtime_error( "Non number binding argument" );
    }
    int64_t res = 0;
    // JS_ToInt64Ext *does* handle ints, floats, and bigints
    int failed = JS_ToInt64Ext( ctx, &res, v );
    if( failed ) {
        // do still more evil
        throw std::runtime_error( "Failed to convert to int." );
    }
    return static_cast<T>( res );
}

template<typename T>
extern auto from_js( JSContext *ctx,
                     JSValueConst v ) -> std::enable_if_t<std::is_same_v<T, std::string>, std::string>;

template<typename T, std::enable_if_t<std::is_integral_v<std::decay_t<T>>>* = nullptr>
                                      inline JSValue to_js( JSContext *ctx, T && t ) noexcept
{
    // need to switch off size and signedness to call appropriate bigint ctor,
    // or else just forbid >32bit ints in the vm
    return JS_NewNumber( ctx, t );
}

template<typename T>
inline auto to_js( JSContext *ctx,
                   T &&t ) noexcept -> std::enable_if_t<std::is_same_v<std::decay_t<T>, std::string>, JSValue>
{
    return JS_NewStringLen( ctx, t.data(), t.size() );
}

namespace
{

// Finding the maximum required number of arguments a function accepts is easy. It's just
// the size of the ...Args parameter pack. Finding the *minimum* is hard for two reasons.
// 1) Overloads, which we can't support for other reasons (if two overloads share the same
//    arity but with differently typed variables, which do we pick to invoke?)
// 2) Default arguments. The values are inserted by the compiler at callsites but are not
//    encoded in the function type at all.
//
// Therefore we generate a unique 'arity tester' helper type per bound function which tests
// whether a member function is invocable with hardcoded calls inside a decltype() so the
// compiler can supplement default args it could not do if we used std::is_invocable_v.
// We can derive the minimum number of arguments a given function can be invoked with though
// with some clever SFINAE and some constexpr calculations which invoke a function with
// increasingly fewer arguments until it runs out or fails.
template<typename ArityTester, typename ArgsTuple, size_t... Argc>
constexpr bool is_callable_with_n( std::index_sequence<Argc...> )
{
    // `std::tuple_element_t<Argc, ArgsTuple>...` expands to a list of types Argc long.
    return ArityTester::template is_callable_with_v<std::tuple_element_t<Argc, ArgsTuple>...>;
}

template<typename ArityTester, typename ArgsTuple, int N>
constexpr int find_min_arity()
{
    if constexpr( N == 0 ) {
        return 0;
    } else if constexpr( !is_callable_with_n<ArityTester, ArgsTuple>(
                             std::make_index_sequence < N - 1 > {} ) ) {
        return N;
    } else {
        return find_min_arity < ArityTester, ArgsTuple, N - 1 > ();
    }
}
}

template<typename T>
struct arg_wrapper {
    JSContext *ctx;
    JSValue *v;

    operator T() {
        return from_js<T>( ctx, *v );
    }
};

template<typename Invoke>
struct js_ffi {
    using R = typename Invoke::ReturnType;
    using ArgsTuple = typename Invoke::ArgsTuple;
    static constexpr int min_arity = Invoke::min_arity;
    static constexpr int max_arity = Invoke::max_arity;

    // *INDENT-OFF*
    // astyle loses its shit over all this template stuff
    template<size_t ...I>
    CATA_FORCEINLINE static JSValue ffi(
        JSContext* ctx,
        void* this_val,
        int argc,
        JSValueConst* argv,
        std::index_sequence<I...>)
    {
        return ffi_(ctx, this_val, argc, argv, arg_wrapper<std::decay_t<std::tuple_element_t<I, ArgsTuple>>>{ctx, &argv[I]}...);
    }

    template<typename... Args, size_t N = sizeof...(Args)>
    CATA_FORCEINLINE static JSValue ffi_(
        JSContext* ctx,
        void* this_val,
        int argc,
        JSValueConst* argv,
        Args&& ...args)
    {
        if constexpr (N == max_arity) {
            return invoke(ctx, this_val, std::forward<Args>(args)...);
        }
        if (N == max_arity) {
            return invoke(ctx, this_val, std::forward<Args>(args)...);
        }
        if constexpr (N < max_arity) {
            return ffi_(ctx, this_val, argc, argv, std::forward<Args>(args)..., arg_wrapper<std::decay_t<std::tuple_element_t<N, ArgsTuple>>>{ctx, &argv[N]});
        } else {
            return JS_UNDEFINED;
        }
    }

    template<typename ...ArgsSlice>
    CATA_FORCEINLINE static JSValue invoke(
        JSContext* ctx,
        void* this_val,
        ArgsSlice &&...args)
    {
        if constexpr (std::is_void_v<R>) {
            Invoke{}(this_val, std::forward<ArgsSlice>(args)...);
            return JS_UNDEFINED;
        } else {
            return to_js(
                ctx,
                Invoke{}(this_val, std::forward<ArgsSlice>(args)...)
            );
        }
    }
    // *INDENT-ON*
};

// Helper type for deducing parts of a function signature.
template <typename T>
struct function_pointer_traits;

// Free functions
template <typename R, typename... Args>
struct function_pointer_traits<R( * )( Args... )> {
    using ReturnType = R;
    using ArgsTuple = std::tuple<Args...>;
    static constexpr int max_arity = sizeof...( Args );
    static constexpr bool is_const = false;
};

// Class member functions
template <typename R, typename C, typename... Args>
struct function_pointer_traits<R( C::* )( Args... )> {
    using ReturnType = R;
    using ArgsTuple = std::tuple<Args...>;
    static constexpr int max_arity = sizeof...( Args );
    static constexpr bool is_const = false;
};

// Because c++, a separate overload for class *const* member functions
template <typename R, typename C, typename... Args>
struct function_pointer_traits<R( C::* )( Args... ) const> {
    using ReturnType = R;
    using ArgsTuple = std::tuple<Args...>;
    static constexpr int max_arity = sizeof...( Args );
    static constexpr bool is_const = true;
};

struct proto_base {
    JSClassID clsid;
    std::vector<JSCFunctionListEntry> bindings;

    void push_erased( std::string_view name, int argc, qjs::generic_cfunc fn );
    static JSValue call_erased( JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv,
                                int min_arity, qjs::generic_cfunc fn );
};

template<typename Clazz>
struct proto : proto_base {
    static proto<Clazz> __proto;
};

// *INDENT-OFF*
#define PROTO(cls) proto<cls> proto<cls>::__proto

#define BIND(cls, func)                                                                                \
namespace                                                                                               \
{                                                                                                       \
    struct cls##__##func##_binding                                                                      \
    {                                                                                                   \
        struct func##_invoker : function_pointer_traits<decltype(&cls::func)>                           \
        {                                                                                               \
            template<                                                                                   \
                typename ...Args,                                                                       \
                typename = decltype(std::declval<cls>().func(std::declval<Args>()...), true)>           \
            static auto test(int) -> std::true_type;                                                    \
            template<typename...> static auto test(...) -> std::false_type;                             \
            template<typename... Args>                                                                  \
            static constexpr bool is_callable_with_v = decltype(test<Args...>(0))::value;               \
                                                                                                        \
            template<typename ...Args, typename = std::enable_if_t<is_callable_with_v<Args...>>>        \
            auto operator()(void* this_val, Args&&... args)                                             \
            {                                                                                           \
                return static_cast<cls*>(this_val)->func(std::forward<Args>(args)...);                  \
            }                                                                                           \
            static constexpr int min_arity = find_min_arity<func##_invoker, ArgsTuple, max_arity>();    \
        };                                                                                              \
                                                                                                        \
        cls##__##func##_binding();                                                                      \
    };                                                                                                  \
                                                                                                        \
    cls##__##func##_binding::cls##__##func##_binding()                                                  \
    {                                                                                                   \
        proto<cls>::__proto.push_erased(                                                                \
            #func,                                                                                      \
            func##_invoker::min_arity,                                                                  \
            [](JSContext* ctx, JSValue this_val, int argc, JSValue* argv) {                             \
                return proto<cls>::__proto.call_erased(                                                 \
                    ctx,                                                                                \
                    this_val,                                                                           \
                    argc,                                                                               \
                    argv,                                                                               \
                    func##_invoker::min_arity,                                                          \
                    [](JSContext* ctx, JSValue this_val, int argc, JSValue* argv) {                     \
                        return js_ffi<func##_invoker>::ffi(                                             \
                            ctx,                                                                        \
                            JS_GetOpaque(this_val, proto<cls>::__proto.clsid),                          \
                            argc,                                                                       \
                            argv,                                                                       \
                            std::make_index_sequence<func##_invoker::min_arity>());                     \
                    });                                                                                 \
            });                                                                                         \
    }                                                                                                   \
                                                                                                        \
    static cls##__##func##_binding cls##__##func##_binder;                                              \
}                                                                                                       \

#endif // CATA_SRC_QJS_BINDINGS_H
