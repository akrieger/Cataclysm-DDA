#include "qjs.h"

#include <qjs/quickjs.h>
#include <qjs/quickjs-libc.h>

#include "input_context.h"
#include "ui_manager.h"

namespace qjs
{

runtime::runtime( JSRuntime *r ) : r{ r } {}

runtime::~runtime()
{
    JS_FreeRuntime( r );
    r = nullptr;
}

std::shared_ptr<runtime> runtime::make()
{
    return std::shared_ptr<runtime>( new runtime{ JS_NewRuntime() } );
}

context::context( JSContext *c, std::shared_ptr<qjs::runtime> r ) : c{ c }, r{ std::move( r ) } {};

context::~context()
{
    JS_FreeContext( c );
    c = nullptr;
}

std::shared_ptr<context> context::make( std::shared_ptr<runtime> r )
{
    JSRuntime *rt = r->get();
    // std::move may evaluate before r->get() unless we separate the calls.
    return std::shared_ptr<context>( new context( JS_NewContext( rt ), std::move( r ) ) );
}

cataimgui::bounds Console::get_bounds()
{
    return { -1.f, -1.f, ImGui::GetMainViewport()->Size.x, ImGui::GetMainViewport()->Size.y };
}

void Console::draw_controls()
{
    ImGui::ShowDemoWindow();
    //draw_lorem( stuff );
}

void Console::init()
{
    // The demo makes it's own screen.  Don't get in the way
    force_to_back = true;
}

void Console::run()
{
    auto r = runtime::make();

    auto c = context::make( r );

    js_std_init_handlers( r->get() );

    JS_SetModuleLoaderFunc( r->get(), nullptr, js_module_loader, nullptr );

    js_std_add_helpers( c->get(), 0, nullptr );

    /* system modules */
    js_init_module_std( c->get(), "std" );
    js_init_module_os( c->get(), "os" );
    static auto f = +[]( JSContext * ctx, JSValueConst this_val, int argc, JSValueConst * argv,
    int magic ) -> JSValue {
        if( argc != 2 )
        {
            debugmsg( "fail" );
        }
        if( !JS_IsNumber( argv[0] ) )
        {
            debugmsg( "no" );
        }
        if( !JS_IsString( argv[1] ) )
        {
            debugmsg( "doublefail" );
        }
        return JS_NULL;
    };

    static const JSCFunctionListEntry funcs[] = {
        js_cfunc_magic_def( "f", 2, f, 0 )
    };
    JSModuleDef *m = JS_NewCModule( c->get(), "funcy", []( JSContext * ctx, JSModuleDef * m ) {
        JS_SetModuleExportList( ctx, m, funcs, 1 );
        return 0;
    } );
    JS_AddModuleExportList( c->get(), m, funcs, 1 );
    JS_AddModuleExport( c->get(), m, "f" );

    std::string script = R"(
import {f} from "funcy";
export const f2 = () => {
    f(2, "hello");    
};)";

    auto ret = JS_Eval( c->get(), script.c_str(), script.size(), "test.js",
                        JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY );

    if( JS_IsException( ret ) ) {
        JSValue exn = JS_GetException( c->get() );
        JSValue m = JS_GetPropertyStr( c->get(), exn, "message" );
        JSValue s = JS_GetPropertyStr( c->get(), exn, "stack" );
        auto sm = JS_ToCString( c->get(), m );
        auto ss = JS_ToCString( c->get(), s );
        JS_FreeValue( c->get(), exn ); // Free the exception object
        JS_FreeValue( c->get(), s ); // Free the exception object
        JS_FreeValue( c->get(), m ); // Free the exception object
    } else {
        ret = JS_EvalFunction( c->get(), ret );
        script = R"(import {f2} from "test.js"; f2();)";
        ret = JS_Eval( c->get(), script.c_str(), script.size(), "<eval>", JS_EVAL_TYPE_MODULE );
        auto exn = JS_GetException( c->get() );
        JSValue m = JS_GetPropertyStr( c->get(), exn, "message" );
        JSValue s = JS_GetPropertyStr( c->get(), exn, "stack" );
        auto sm = JS_ToCString( c->get(), m );
        auto ss = JS_ToCString( c->get(), s );
        JS_FreeValue( c->get(), exn ); // Free the exception object
        JS_FreeValue( c->get(), s ); // Free the exception object
        JS_FreeValue( c->get(), m ); // Free the exception object
    }

    init();

    input_context ctxt( "HELP_KEYBINDINGS" );
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "SELECT" );
    ctxt.register_action( "MOUSE_MOVE" );
    ctxt.register_action( "ANY_INPUT" );
    ctxt.register_action( "HELP_KEYBINDINGS" );
    std::string action;

    ui_manager::redraw();

    while( is_open ) {
        ui_manager::redraw();
        action = ctxt.handle_input( 5 );
        if( action == "QUIT" ) {
            break;
        }
    }
}

value value::prop( std::string_view key ) const
{
    JSValue message_value = JS_GetPropertyStr( ctx->get(), v, key.data() );
    return value{ ctx, message_value };
}

cstring value::to_cstring() const
{
    return cstring{ ctx, v };
}

exception value::to_exception() const &
{
    return clone().to_exception();
}

exception value::to_exception()&& {
    if( !JS_IsException( v ) )
    {
        // idk throw?
    }
    return exception( std::move( *this ) );
}

string value::to_string() const &
{
    return clone().to_string();
}

string value::to_string()&& {
    if( !JS_IsString( v ) )
    {
        // idk throw?
    }
    return string( std::move( *this ) );
}

JSCFunctionListEntry js_cfunc_magic_def( const char *name, int length, generic_magic func1,
        int magic )
{
    JSCFunctionListEntry entry;
    entry.name = name;
    entry.prop_flags = JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE;
    entry.def_type = JS_DEF_CFUNC;
    entry.magic = magic;
    entry.u.func.length = length;
    entry.u.func.cproto = JS_CFUNC_generic_magic;
    entry.u.func.cfunc.generic_magic = func1;
    return entry;

}

}
