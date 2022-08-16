#if !defined(_MSC_VER)
#include <iosfwd>
#include <string>

// the rewrite_vsnprintf function is explicitly defined for non-MS compilers in output.cpp
#include "cata_catch.h"
#include "output.h"

TEST_CASE( "Test vsnprintf_rewrite" )
{
    CHECK( rewrite_vsnprintf( "%{}%%" ) == "%{}%%" );
    CHECK( rewrite_vsnprintf( "hello" ) == "hello" );
    CHECK( rewrite_vsnprintf( "%%" ) == "%%" );
    CHECK( rewrite_vsnprintf( "" ).empty() );
    CHECK( rewrite_vsnprintf( "{}" ) == "{}" );
    CHECK( rewrite_vsnprintf( "{}" ) == "{}" );
    CHECK( rewrite_vsnprintf( "{}" ) == "{}" );
    CHECK( rewrite_vsnprintf( "{1}" ) == "{}" );
    CHECK( rewrite_vsnprintf( "{2}" ) == "{2}" );
    CHECK( rewrite_vsnprintf( "{1}.4f" ) == "{}.4f" );
    CHECK( rewrite_vsnprintf( "{1}.4f {}" ) == "{1}.4f {}" );
    CHECK( rewrite_vsnprintf( "{1}.4f {2}" ) == "{}.4f {}" );
    CHECK( rewrite_vsnprintf( "%" ) == "%" );
    CHECK( rewrite_vsnprintf( "{1} {1}" ) == "{1} {1}" );
    CHECK( rewrite_vsnprintf( "{2} {1} {3} {4} {5} {6} {7} {8} {9} {10} {11}" ) ==
           "{2} {1} {3} {4} {5} {6} {7} {8} {9} <formatting error> <formatting error>" );

    CHECK( rewrite_vsnprintf( "{1} {2} {3} {4} {5} {6} {7} {8} {9} {10} {11}" ) ==
           "{} {} {} {} {} {} {} {} {} {} {}" );

    CHECK( rewrite_vsnprintf( "Needs <color_{1}>{2}</color>, a <color_{3}>wrench</color>, "
                              "either a <color_{4}>powered welder</color> "
                              "(and <color_{5}>welding goggles</color>) or "
                              "<color_{6}>duct tape</color>, and level <color_{7}>{8}</color> "
                              "skill in mechanics.{9}{10}" )  ==
           "Needs <color_{}>{}</color>, a <color_{}>wrench</color>, either a "
           "<color_{}>powered welder</color> (and <color_{}>welding goggles</color>) or "
           "<color_{}>duct tape</color>, and level <color_{}>{}</color> skill in mechanics.{}{}" );
}

#endif
