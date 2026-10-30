#include <string>

#include <qjs/quickjs.h>

#include "qjs_bindings.h"

template<>
std::string from_js<std::string>( JSContext *ctx, JSValueConst v )
{
    // need to check convertibility and set a VM error
    if( !JS_IsString( v ) ) {
        // not the right error
        throw std::runtime_error( "Non string arg" );
    }
    size_t len;
    const char *str = JS_ToCStringLen( ctx, &len, v );
    if( !str ) {
        // not the right error
        throw std::runtime_error( "Failed to convert arg to string" );
    }
    std::string s{ str, len };
    JS_FreeCString( ctx, str );
    return s;
}
