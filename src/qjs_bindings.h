#pragma once
#ifndef CATA_SRC_QJS_BINDINGS_H
#define CATA_SRC_QJS_BINDINGS_H

#include <string_view>
#include <vector>

#include <qjs/quickjs.h>

#include "qjs.h"

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
    } else if constexpr( !is_callable_with_n<ArityTester, ArgsTuple>( std::make_index_sequence < N - 1
                         > {} ) ) {
        return N;
    } else {
        return find_min_arity < ArityTester, ArgsTuple, N - 1 > ();
    }
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnon-virtual-dtor"
struct type_erasing_wrapper {
    protected:
        virtual ~type_erasing_wrapper();
    public:
        virtual JSValue call( JSContext *ctx, void *this_val, int argc, JSValueConst *argv ) noexcept = 0;
};

template<typename Binding, typename ArityTester, auto MemFn>
struct member_function_wrapper;

// placeholders
template<typename T>
T from_js( JSContext *ctx, JSValueConst v ) noexcept;

template<typename T>
JSValue to_js( JSContext *ctx, T &&t ) noexcept;

template<typename Binding, typename ArityTester,
         typename C, typename R, typename... Args, R( C::* MemFn )( Args... )>
struct member_function_wrapper<Binding, ArityTester, MemFn> : type_erasing_wrapper {
        using ArgsTuple = std::tuple<Args...>;
        static constexpr int max_arity = sizeof...( Args );
        static constexpr int min_arity =
            std::integral_constant<int, find_min_arity<ArityTester, ArgsTuple, max_arity>()>::value;
        virtual JSValue call( JSContext *ctx, void *this_val, int argc,
                              JSValueConst *argv ) noexcept override {
            return switch_arity<min_arity>( ctx, static_cast<C *>( this_val ), argc, argv );
        }

    private:
        // Automagically generate the appropriate call_n invocation for every value from min_arity to max_arity.
        // Eg. assume a function takes between 2 and 4 args and argc is 3.
        // switch_arity<2> is the first call, falls into the if constexpr block, calls switch_arity<3>
        // switch_arity<3> calls call_n(ctx, this_val, argv, std::make_index_sequence<3>{})
        // call_n(...) calls Binding::call(*this_val, from_js(ctx, argv[0]), from_js(ctx, argv[1]), from_js(ctx, argv[2]));
        template<int N>
        static JSValue switch_arity( JSContext *ctx, C *this_val, int argc, JSValueConst *argv ) noexcept {
            if( argc <= N || N == max_arity ) {
                return call_n( ctx, this_val, argv, std::make_index_sequence<N> {} );
            }
            if constexpr( N < max_arity ) {
                return switch_arity < N + 1 > ( ctx, this_val, argc, argv );
            }
            return JS_UNDEFINED;
        }

        template<size_t... I>
        static JSValue call_n( JSContext *ctx, C *this_val, JSValueConst *argv,
                               std::index_sequence<I...> ) noexcept {
            if constexpr( std::is_void_v<R> ) {
                Binding::call(
                    *this_val,
                    from_js<std::tuple_element_t<I, ArgsTuple>>( ctx, argv[I] )...
                );
                return JS_UNDEFINED;
            } else {
                return to_js(
                           ctx,
                           Binding::call(
                               *this_val,
                               from_js<std::tuple_element_t<I, ArgsTuple>>( ctx, argv[I] )...
                           )
                       );
            }
        }
};

struct proto_base {
    static void push_erased(
        std::vector<JSCFunctionListEntry> &bindings,
        std::vector<type_erasing_wrapper *> &funcs,
        std::string_view name,
        int argc,
        qjs::generic_magic call,
        type_erasing_wrapper *fn
    ) noexcept;
};

template<typename Clazz>
struct proto : proto_base {
    using Class = Clazz;
    static JSClassID clsid;
    static std::vector<JSCFunctionListEntry> bindings;
    static std::vector<type_erasing_wrapper *> funcs;

    static JSValue call( JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv,
                         int magic ) noexcept {
        return funcs[magic]->call( ctx, JS_GetOpaque( this_val, clsid ), argc, argv );
    }

    static void push( std::string_view name, int argc, type_erasing_wrapper *fn ) noexcept {
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
    /* This struct definition has to be broken out separately so it can be properly consumed in the */          \
    /* member_function_wrapper definition, otherwise the compiler gets its knickers in a twist with */          \
    /* self-referential recursive constexpr functions and otherwise is sad. */                                  \
    struct func##_arity_tester {                                                                                \
        /* The below gnarly template funk is a hairball of SFINAE helpers for determining the minimum */        \
        /* and maximum number of arguments a function can be invoked with. */                                   \
        /* This is the 'good' test function. If and only if the decltype expression is well formed */           \
        /* will the overload exist and be callable. The comma operator inside the decltype means the */         \
        /* computed return type will be std::true_type and have ::value = true. */                              \
        /* This overload is preferred over the other because it is a more specific match under normal*/         \
        /* overload resolution rules. */                                                                        \
        template<typename... Args>                                                                              \
        static auto test(int)                                                                                   \
        -> decltype(std::declval<decltype(__proto)::Class&>().func(std::declval<Args>()...), std::true_type{}); \
        /* The bad overload matches anything because of the ... argument. So whenever test(int) is not */       \
        /* selected, i.e. when SFINAE removes it because the func call cannot succeed with that many args, */   \
        /* then we get std::false_type as the type and callable then is false. */                               \
        template<typename...>                                                                                   \
        static auto test(...) -> std::false_type;                                                               \
        /* true if C.func(Args...) is well formed, false otherwise. */                                          \
        template<typename... Args>                                                                              \
        static constexpr bool is_callable_with_v = decltype(test<Args...>(0))::value;                           \
    };                                                                                                          \
    struct func##_binding : member_function_wrapper<                                                            \
        func##_binding,                                                                                         \
        func##_arity_tester,                                                                                    \
    /**/&decltype(__proto)::Class::func                                                                         \
    > {                                                                                                         \
        func##_binding() noexcept;                                                                              \
        template<typename... Args>                                                                \
        /* this triggers ICE */ \
        /*static decltype(auto) decltype(__proto)::Class::func##_binding::call(decltype(__proto)::Class& val, Args&&... args) noexcept  */ \
        static decltype(auto) call(decltype(__proto)::Class& val, Args&&... args) noexcept               \
        {                                                                                         \
            try {                                                                                 \
                return val.func(std::forward<Args>(args)...);                                     \
            } catch(...) {                                                                        \
                if constexpr (!std::is_void_v<decltype(val.func(std::forward<Args>(args)...))>) { \
                    return decltype(val.func(std::forward<Args>(args)...)){};                     \
                }                                                                                               \
            }                                                                                                   \
        }                                                                                                       \
    };                                                                                                          \
    static func##_binding __##func##_binder
// *INDENT-ON

#define BINDABLE(cls) static proto<cls> __proto

#pragma clang diagnostic pop

#define PROTO(cls) \
    proto<cls> cls::__proto; \
    template<> \
    JSClassID proto<cls>::clsid; \
    template<> \
    std::vector<JSCFunctionListEntry> proto<cls>::bindings; \
    template<> \
    std::vector<type_erasing_wrapper *> proto<cls>::funcs

#define BOUND(cls, func)                                                                      \
    cls::func##_binding cls::__##func##_binder;                                               \
    cls::func##_binding::func##_binding() noexcept { __proto.push(#func, min_arity, this); }

#endif // CATA_SRC_QJS_BINDINGS_H
