#include "qjs.h"

#include <qjs/quickjs.h>

#include "input_context.h"
#include "ui_manager.h"

namespace qjs
{

Runtime::Runtime( JSRuntime *r ) : runtime{ r } {}

Runtime::~Runtime()
{
    JS_FreeRuntime( runtime );
    runtime = nullptr;
}

std::shared_ptr<Runtime> Runtime::make()
{
    return std::shared_ptr<Runtime>( new Runtime{ JS_NewRuntime() } );
}

Context::Context( JSContext *c, std::shared_ptr<Runtime> r ) : context{ c }, runtime{ std::move( r ) } {};

Context::~Context()
{
    JS_FreeContext( context );
    context = nullptr;
}

std::shared_ptr<Context> Context::make( std::shared_ptr<Runtime> r )
{
    JSRuntime *runtime = r->get();
    // std::move may evaluate before r->get() unless we separate the calls.
    return std::shared_ptr<Context>( new Context( JS_NewContext( runtime ), std::move( r ) ) );
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
