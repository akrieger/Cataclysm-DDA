#ifndef CATA_SRC_CATA_FLATBUFFERS_ASSERT_H
#define CATA_SRC_CATA_FLATBUFFERS_ASSERT_H

// A substitute for assert to be used in flatbuffers / flexbuffers so that parsing errors
// raise a catchable error like currently happens with JsonObject/JsonIn

#ifdef FLATBUFFERS_ASSERT
#undef FLATBUFFERS_ASSERT
#endif

#include <cstdio>

#include "json_error.h"

#define FLATBUFFERS_ASSERT(expression) \
    do { \
        if( expression ) { \
            break; \
        } \
        char buf[1024]{}; \
        snprintf( buf, 1023, "%s at %s:%d: Assertion `%s` failed.\n", __func__, __FILE__, __LINE__, #expression ); \
        buf[1023] = '\0'; \
        throw JsonError( buf ); \
    } while( false );

#endif
