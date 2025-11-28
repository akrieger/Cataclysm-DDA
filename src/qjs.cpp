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

    std::string script = R"(console.log(yolo,))";

    auto ret = JS_Eval( c->get(), script.c_str(), script.size(), "<input>", JS_EVAL_FLAG_COMPILE_ONLY );

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

}


