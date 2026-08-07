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

extern JSCFunctionListEntry js_cfunc_magic_def( const char *name, int length, generic_magic func1,
        int magic );

}

template<typename>
struct arity {};

template<typename R, typename ... Args>
struct arity<R( Args... )> : std::integral_constant<int, sizeof...( Args )> {
};

template<typename C, typename R, typename ... Args>
struct arity<R( C::* )( Args... )> : arity<R( Args... )> {
};

template<typename F>
constexpr auto arity_v( F fn )
{
    return arity<decltype( fn )>::value;
}


struct type_erasing_wrapper {
    virtual JSValue call( JSContext *ctx, void *this_val, int argc, JSValueConst *argv ) = 0;
};

template<typename>
struct wrapper {};

template<typename R, typename ... Args>
struct wrapper<R( Args... )> : type_erasing_wrapper {
    using Callable = R( Args... );
    static constexpr int arity = sizeof...( Args );
};

template<typename T, typename DecayT = std::decay_t<T>>
JSValue wrap_return_value( T && t )
{
    if constexpr( std::is_integral_v<DecayT> ) {

    } else if constexpr( std::is_floating_point_v<DecayT> ) {

    } else if constexpr( std::is_constructible_v<std::string_view, DecayT> ) {

    }
}

template<typename C, typename R, typename ... Args>
struct value_member_fn_wrapper : type_erasing_wrapper {
    using Callable = R( C::* )( Args... );
    static constexpr int arity = sizeof...( Args );

    JSValue call( JSContext *ctx, void *this_val, R( C::*func )( Args... ), int argc,
                  JSValueConst *argv ) {
        static_cast<C *>( this_val )->*func( argc, argv );
    }
};

template<typename C, typename ... Args>
struct void_member_fn_wrapper : type_erasing_wrapper {
    using Callable = void ( C::* )( Args... );
    static constexpr int arity = sizeof...( Args );

    JSValue call2( JSContext *ctx, void *this_val, void ( C::*func )( Args... ), int argc,
                   JSValueConst *argv ) {
        ( static_cast<C *>( this_val )->*func )( 0, "argv" );
        return JS_NULL;
    }
};

template<typename T>
struct carrier {
    using type = T;
};

template<typename C, typename ... Args>
constexpr auto deduce_wrapper_from_member( void ( C::* )( Args... ) )
{
    return carrier<void_member_fn_wrapper<C, Args...>> {};
}

template<typename C, typename R, typename ... Args>
constexpr auto deduce_wrapper_from_member( R( C::* )( Args... ) )
{
    return carrier<value_member_fn_wrapper<C, R, Args...>> {};
}

template<typename Clazz>
struct proto {
    using Class = Clazz;
    static JSClassID clsid;
    static std::vector<JSCFunctionListEntry> bindings;
    static std::vector<type_erasing_wrapper *> funcs;

    static JSValue call( JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv,
                         int magic ) {
        return funcs[magic]->call( ctx, JS_GetOpaque( this_val, clsid ), argc, argv );
    }

    void push( std::string_view name, int argc, type_erasing_wrapper *fn ) {
        bindings.emplace_back( qjs::js_cfunc_magic_def( name.data(), argc, &proto::call, funcs.size() ) );
        funcs.emplace_back( fn );
    }
};

#define CAT(x, y) x##y
#define CAT2(x, y) CAT(x, y)
#define CAT3(x, y, z) CAT2(x, CAT2(y, z))

#define  BIND(func) BIND1(func, __COUNTER__)
#define BIND1(func, counter) BIND2(func, CAT3(func, _binder, counter))
#define BIND2(func, binder) \
    struct binder : decltype( deduce_wrapper_from_member( &decltype( proto_ )::Class::foo ) )::type { \
        binder() { \
            proto_.push(#func, arity_v(&decltype(proto_)::Class::func), this); \
        } \
        virtual JSValue call(JSContext* ctx, void* this_val, int argc, JSValueConst* argv) \
        { \
            return call(ctx, this_val, &decltype(proto_)::Class::func, argc, argv); \
        } \
    }; \
    static binder b##counter;

#define BINDABLE(cls) \
    protected: \
    static proto<cls> proto_; \
    public:

struct bound {
    BINDABLE( bound );

    void foo( int, std::string );
    struct foo_binder4 : decltype( deduce_wrapper_from_member( &decltype( proto_ )::Class::foo ) )
    ::type {
        foo_binder4() {
            proto_.push( "foo", arity_v( &decltype( proto_ )::Class::foo ), this );
        }
        virtual JSValue call( JSContext *ctx, void *this_val, int argc, JSValue *argv ) {
            return call2( ctx, this_val, &decltype( proto_ )::Class::foo, argc, argv );
        }
    };
    static foo_binder4 bcounter;;
};
