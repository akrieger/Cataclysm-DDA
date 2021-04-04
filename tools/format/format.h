#ifndef CATA_TOOLS_FORMAT_H
#define CATA_TOOLS_FORMAT_H

class TextJsonIn;
class FlexJsonValue; using JsonIn = FlexJsonValue;
class TextJsonOut;
using JsonOut = TextJsonOut;

namespace formatter
{
void format( JsonIn &jsin, JsonOut &jsout, int depth = -1, bool force_wrap = false );
}

#endif
