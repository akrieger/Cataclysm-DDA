#include <memory>

#include <qjs/quickjs.h>

#include "cata_imgui.h"

namespace qjs
{

class context;

class runtime : public std::enable_shared_from_this<runtime>
{
        runtime( JSRuntime *r );
    public:
        ~runtime();

        static std::shared_ptr<runtime> make();

        JSRuntime *get() {
            return r;
        }

    private:
        JSRuntime *r;
};

class value;

class context : public std::enable_shared_from_this<context>
{
        context( JSContext *c, std::shared_ptr<runtime> r );
    public:
        ~context();
        static std::shared_ptr<context> make( std::shared_ptr<runtime> r );

        JSContext *get() {
            return c;
        }

    private:
        JSContext *c;
        std::shared_ptr<runtime> r;
};

class Console : cataimgui::window
{
    public:
        Console( ) : cataimgui::window( "qjs console" ), ctx{  } {};
        void init();
        void run();
        void draw_controls() override;
        cataimgui::bounds get_bounds() override;
        void on_resized() override {
            init();
        }
    private:
        std::shared_ptr<context> ctx;
};

class cstring
{
    public:
        cstring( std::shared_ptr<context> ctx, JSValue v ) : ctx{ ctx } {
            const char *cstr;
            size_t len;
            cstr = JS_ToCStringLen( ctx->get(), &len, v );
            str = { cstr, len };
        }

        ~cstring() {
            free();
        }

        cstring( cstring const & ) noexcept = delete;
        cstring &operator=( cstring const & ) noexcept = delete;

        cstring( cstring &&rhs ) noexcept : ctx{ std::move( rhs.ctx ) }, str{ rhs.str } {
            rhs.str = {};
        }
        cstring &operator=( cstring &&rhs ) noexcept {
            if( &rhs == this ) {
                return *this;
            }
            free();
            ctx = std::move( rhs.ctx );
            str = rhs.str;
            rhs.str = {};
            return *this;
        }

        operator std::string_view() const {
            return str;
        }

    private:
        std::shared_ptr<context> ctx;
        std::string_view str;

        void free() {
            if( ctx && str.data() ) {
                JS_FreeCString( ctx->get(), str.data() );
            }
        }
};

class exn;
class number;
class string;

class value
{
        friend class context;
    public:
        value( std::shared_ptr<context> ctx, JSValue v ) : ctx{ std::move( ctx ) }, v{ v } {}
        value( std::nullptr_t, JSValue ) = delete;

        ~value() {
            free();
        }

        // No implicit copies maybe.
        value( value const &rhs ) = delete;
        value &operator=( value const &rhs ) = delete;

        value( value &&rhs ) noexcept : ctx{ std::move( rhs.ctx ) }, v{ rhs.v } {}
        value &operator=( value &&rhs ) noexcept {
            if( &rhs == this ) {
                return *this;
            }
            free();
            ctx = std::move( rhs.ctx );
            v = rhs.v;
            return *this;
        }

        // Explicit copies.
        value clone() const & {
            if( ctx ) {
                JS_DupValue( ctx->get(), v );
            }
            return value{ ctx, v };
        }
        value clone() && {
            return std::move( *this );
        }

        value idx( int key ) const;
        value prop( std::string_view key ) const;

        cstring to_cstring() const;

        exn to_exception() const&;
        exn to_exception() &&;

        number to_number() const&;
        number to_number() &&;

        string to_string() const&;
        string to_string() &&;

    protected:
        std::shared_ptr<context> ctx;
        JSValue v;

    private:
        void free() {
            if( ctx ) {
                JS_FreeValue( ctx->get(), v );
            }
        }
};

class number : value
{
    public:
        number( value &&v ) : value( std::move( v ) ) {}

        int to_int() const {
            int ret;
            JS_ToInt32( ctx->get(), &ret, v );
            return ret;
        }

        double to_double() const {
            double ret;
            JS_ToFloat64( ctx->get(), &ret, v );
            return ret;
        }
};

class string : value
{
    public:
        string( value &&v ) : value{ std::move( v ) } {}

        cstring to_cstring() const {
            return value::to_cstring();
        }
};

class exn : value
{
    public:
        exn( value &&v ) : value{ std::move( v ) } {}

        string get_message() const {
            return prop( "message" ).to_string();
        }

        string get_stack() const {
            return prop( "stack" ).to_string();
        }
};

using generic_magic = JSValue( * )( JSContext *ctx, JSValueConst this_val, int argc,
                                    JSValueConst *argv, int magic );

extern JSCFunctionListEntry js_cfunc_magic_def(
    const char *name,
    int length,
    generic_magic func1,
    int magic );

}

// Finding the maximum required number of arguments a function accepts is easy. It's just
// the size of the ...Args parameter pack. Finding the *minimum* is hard for two reasons.
// 1) Overloads, which we can't support for other reasons (if two overloads share the same
//    arity but with differently typed variables, which do we pick to invoke?)
// 2) Default arguments. The values are inserted by the compiler at callsites but are not
//    encoded in the function type at all.
// We can derive the minimum number of arguments a given function can be invoked with though
// with some clever SFINAE and some constexpr calculations which invoke a function with
// increasingly fewer arguments until it runs out or fails.
template<typename Binding, typename ArgsTuple, size_t... Argc>
constexpr bool is_callable_with_n( std::index_sequence<Argc...> )
{
    // `std::tuple_element_t<Argc, ArgsTuple>...` expands to a list of types Argc long.
    return Binding::template is_callable_with_v<std::tuple_element_t<Argc, ArgsTuple>...>;
}

template<typename Binding, typename ArgsTuple, int N>
constexpr int find_min_arity()
{
    if constexpr( N == 0 ) {
        return 0;
    } else if constexpr( !is_callable_with_n<Binding, ArgsTuple>( std::make_index_sequence < N - 1 > {} ) ) {
        return N;
    } else {
        return find_min_arity < Binding, ArgsTuple, N - 1 > ();
    }
}

struct type_erasing_wrapper {
    virtual JSValue call( JSContext *ctx, void *this_val, int argc, JSValueConst *argv ) = 0;
};

template<typename Binding, auto MemFn>
struct member_function_wrapper;

// placeholders
template<typename T>
T from_js( JSContext *ctx, JSValueConst v );

template<typename T>
JSValue to_js( JSContext *ctx, T &&t );

template<typename Binding,
         typename C, typename R, typename... Args, R( C::* MemFn )( Args... )>
struct member_function_wrapper<Binding, MemFn> : type_erasing_wrapper {
        using ArgsTuple = std::tuple<Args...>;
        static constexpr int max_arity = sizeof...( Args );
        static constexpr int min_arity = find_min_arity<Binding, ArgsTuple, max_arity>();

        virtual JSValue call( JSContext *ctx, void *this_val, int argc, JSValueConst *argv ) override {
            return switch_arity<min_arity>( ctx, static_cast<C *>( this_val ), argc, argv );
        }

    private:
        // Automagically generate the appropriate call_n invocation for every value from min_arity to max_arity.
        // Eg. assume a function takes between 2 and 4 args and argc is 3.
        // switch_arity<2> is the first call, falls into the if constexpr block, calls switch_arity<3>
        // switch_arity<3> calls call_n(ctx, this_val, argv, std::make_index_sequence<3>{})
        // call_n(...) calls Binding::call(*this_val, from_js(ctx, argv[0]), from_js(ctx, argv[1]), from_js(ctx, argv[2]));
        template<int N>
        static JSValue switch_arity( JSContext *ctx, C *this_val, int argc, JSValueConst *argv ) {
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
                               std::index_sequence<I...> ) {
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

struct proto_base
{
    static void push_erased(
        std::vector<JSCFunctionListEntry>& bindings,
        std::vector<type_erasing_wrapper*>& funcs,
        std::string_view name,
        int argc,
        qjs::generic_magic call,
        type_erasing_wrapper* fn
    );
};

template<typename Clazz>
struct proto : proto_base {
    using Class = Clazz;
    static JSClassID clsid;
    static std::vector<JSCFunctionListEntry> bindings;
    static std::vector<type_erasing_wrapper*> funcs;

    static JSValue call(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv,
        int magic)
    {
        return funcs[magic]->call(ctx, JS_GetOpaque(this_val, clsid), argc, argv);
    }

    static void push(std::string_view name, int argc, type_erasing_wrapper* fn)
    {
        push_erased(
            bindings,
            funcs,
            name,
            argc,
            &proto::call,
            fn);
    }
};

// *INDENT-OFF
#define BIND(func)                                                                                              \
    struct func##_binding : member_function_wrapper<                                                            \
        func##_binding,                                                                                         \
    /**/&decltype(__proto)::Class::func                                                                         \
    > {                                                                                                         \
        func##_binding() { __proto.push(#func, min_arity, this); }                                              \
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
        template<typename... Args>                                                                              \
        static decltype(auto) call(decltype(__proto)::Class& val, Args&&... args)                               \
        {                                                                                                       \
            return val.func(std::forward<Args>(args)...);                                                       \
        }                                                                                                       \
    };                                                                                                          \
    static inline func##_binding __##func##_binder;
// *INDENT-ON

#define BINDABLE(cls) \
    protected: \
    static proto<cls> __proto; \
    public:

struct bound {
    BINDABLE( bound );
    void foo( int, std::string, int = 0 );
    BIND( foo );
    std::string bar(std::string, int, double = 0.0, std::string = "");
    BIND(bar);
};
