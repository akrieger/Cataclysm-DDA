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
template<auto Func>
struct arity_tester {
    /* The below gnarly template funk is a hairball of SFINAE helpers for determining the minimum */
    /* and maximum number of arguments a function can be invoked with. */
    /* This is the 'good' test function. */
    /* This overload is preferred over the other because it is a more specific match under normal*/
    /* overload resolution rules. It only exists if func is invokable with Args */
    template <
        typename ...Args,
        typename = std::enable_if_t<std::is_invocable_v<Func, Args...> >>
    static auto test( int ) -> std::true_type;
    /* The bad overload matches anything because of the ... argument. So whenever test(int) is */
    /* removed by SFINAE then we get std::false_type as the type and callable then is false. */
    template<typename...>
    static auto test( ... ) -> std::false_type;
    /* true if C.func(Args...) is well formed, false otherwise. */
    template<typename... Args>
    static constexpr bool is_callable_with_v = decltype( test<Args...>( 0 ) )::value;
};

// Finding the maximum required number of arguments a function accepts is easy. It's just
// the size of the ...Args parameter pack. Finding the *minimum* is hard for two reasons.
// 1) Overloads, which we can't support for other reasons (if two overloads share the same
//    arity but with differently typed variables, which do we pick to invoke?)
// 2) Default arguments. The values are inserted by the compiler at callsites but are not
//    encoded in the function type at all.
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

template<auto MemFn>
struct member_function_wrapper;

template<typename C, typename R, typename... Args, R( C::* MemFn )( Args... )>
struct member_function_wrapper<MemFn> {
    using ArgsTuple = std::tuple<Args...>;
    static constexpr int max_arity = sizeof...( Args );
    static constexpr int min_arity =
        std::integral_constant<int, find_min_arity<arity_tester<MemFn>, std::tuple<C, Args...>, max_arity>()>::value;
    static JSValue call( JSContext *ctx, C *this_val, int argc,
                         JSValueConst *argv ) {
        return call( ctx, this_val, argc, argv, std::make_index_sequence<min_arity>() );
    }

private:
    // *INDENT-OFF*
    // astyle loses its shit over all this template stuff
    template<size_t ...I>
    CATA_FORCEINLINE static JSValue call(
            JSContext *ctx,
            C *this_val,
            int argc,
            JSValueConst *argv,
            std::index_sequence<I...> ) {
        // This would be nice, sadly its c++26
        // auto&& [...args] = std::forward_as_tuple(from_js<std::decay_t<std::tuple_element_t<I, ArgsTuple>>>(ctx, argv[I])...);
        if constexpr (min_arity == max_arity) {
            return call_binding(ctx, this_val, from_js<std::decay_t<std::tuple_element_t<I, ArgsTuple>>>(ctx, argv[I])...);
        } else {
            return switch_arity(ctx, this_val, argc, argv, from_js<std::decay_t<std::tuple_element_t<I, ArgsTuple>>>(ctx, argv[I])...);
        }
    }

    // N is the number of args in ...args
    // argc is the number of args in argv
    // We recursively call switch_arity with increasingly more args from argv converted
    // with from_js until we hit argc or max_arity. Then we forward to call_binding.
    // With the right inlining, some compilers (like clang) can elide
    // all the recursive calls and just unwrap exactly the right number of args in one
    // clean block of code, directly into the appropriate argument slots for the underlying
    // bound function. Just nice clean code.
    template<typename ...ArgsSlice, size_t N = sizeof...(ArgsSlice)>
    CATA_FORCEINLINE static JSValue switch_arity(
            JSContext *ctx,
            C *this_val,
            int argc,
            JSValueConst *argv,
            ArgsSlice &&...args ) {
        if( argc <= N || N == max_arity ) {
            return call_binding( ctx, this_val, std::forward<ArgsSlice>( args )... );
        }
        if constexpr( N < max_arity ) {
            return switch_arity( ctx, this_val, argc, argv, std::forward<ArgsSlice>( args )...,
                                            from_js<std::decay_t<std::tuple_element_t<N, ArgsTuple>>>( ctx, argv[N] ) );
        }
        return JS_UNDEFINED;
    }

    template<typename ...ArgsSlice>
    CATA_FORCEINLINE static JSValue call_binding(
            JSContext *ctx,
            C *this_val,
            ArgsSlice &&...args ) {
        if constexpr( std::is_void_v<R> ) {
            (this_val->*MemFn)(std::forward<ArgsSlice>(args)...);
            return JS_UNDEFINED;
        } else {
            return to_js(
                ctx,
                (this_val->*MemFn)(std::forward<ArgsSlice>(args)...)
            );
        }
    }
    // *INDENT-ON*
};

using type_erased_wrapper = JSValue( * )( JSContext *, void *, int, JSValueConst * );

struct proto_base {
    static void push_erased(
        std::vector<JSCFunctionListEntry> &bindings,
        std::vector<type_erased_wrapper> &funcs,
        std::string_view name,
        int argc,
        qjs::generic_magic call,
        type_erased_wrapper fn
    ) noexcept;

    static JSValue call_erased( type_erased_wrapper fn, JSContext *ctx, void *this_val, int min_arity,
                                int argc, JSValueConst *argv ) noexcept;
};

template<typename Clazz>
struct proto : proto_base {
    using Class = Clazz;
    static JSClassID clsid;
    static std::vector<JSCFunctionListEntry> bindings;
    static std::vector<type_erased_wrapper> funcs;

    static JSValue call( JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv,
                         int magic ) noexcept {
        return call_erased( funcs[magic], ctx, JS_GetOpaque( this_val, clsid ),
                            bindings[magic].u.func.length,
                            argc, argv );
    }

    static void push( std::string_view name, int argc, type_erased_wrapper fn ) noexcept {
        push_erased(
            bindings,
            funcs,
            name,
            argc,
            &proto::call,
            fn
        );
    }
};

// *INDENT-OFF
#define BIND(func)                                                                                              \
    struct func##_binding : member_function_wrapper<&decltype(__proto)::Class::func> {                          \
        func##_binding() noexcept;                                                                              \
    };                                                                                                          \
    static func##_binding __##func##_binder
// *INDENT-ON

#define BINDABLE(cls) static proto<cls> __proto

#define PROTO(cls) \
    proto<cls> cls::__proto; \
    template<> \
    JSClassID proto<cls>::clsid = {}; \
    template<> \
    std::vector<JSCFunctionListEntry> proto<cls>::bindings{}; \
    template<> \
    std::vector<type_erased_wrapper> proto<cls>::funcs{}

#define BOUND(cls, func)                                                                      \
    cls::func##_binding::func##_binding() noexcept { __proto.push(#func, min_arity, [](JSContext *ctx, void *this_val, int argc, JSValueConst* argv){ return call(ctx, static_cast<cls*>(this_val), argc, argv);}); }  \
    cls::func##_binding cls::__##func##_binder

#endif // CATA_SRC_QJS_BINDINGS_H
