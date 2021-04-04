#pragma once
#ifndef CATA_SRC_JSON_H
#define CATA_SRC_JSON_H

#include "text_json.h"

using JsonError = TextJsonError;
using JsonSerializer = TextJsonSerializer;
using JsonDeserializer = TextJsonDeserializer;
using JsonIn = TextJsonIn;
using JsonOut = TextJsonOut;

#include "flexbuffer_json.h"

using JsonObject = FlexJsonObject;
using JsonArray = FlexJsonArray;
using JsonValue = FlexJsonValue;
using JsonMember = FlexJsonMember;

#endif // CATA_SRC_JSON_H
