#include "qjs.h"

#include <qjs/quickjs.h>

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

}
