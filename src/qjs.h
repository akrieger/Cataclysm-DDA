#include <memory>

#include <qjs/quickjs.h>

#include "cata_imgui.h"

namespace qjs
{

class context;

class runtime
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

class context
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

        cstring( cstring &&rhs ) noexcept : ctx{ std::move( rhs.ctx ) }, str{ rhs.str } {}
        cstring &operator=( cstring &&rhs ) noexcept {
            free();
            ctx = std::move( rhs.ctx );
            str = rhs.str;
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
