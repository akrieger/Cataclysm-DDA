#include "cata_catch.h"
#include "distribution.h"

TEST_CASE( "poisson_distribution", "[distribution]" )
{
    std::string s = R"({ "poisson": 10 })";
    JsonValue jin = JsonValue::fromString( s );
    int_distribution d;
    d.deserialize( jin );

    CHECK( d.description() == "Poisson(10)" );
    CHECK( d.minimum() == 0 );
}
