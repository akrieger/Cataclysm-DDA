#include "morale.h"
#include "morale_types.h"

#include "cata_utility.h"
#include "debug.h"
#include "item.h"
#include "itype.h"
#include "output.h"
#include "bodypart.h"
#include "catacharset.h"
#include "game.h"
#include "weather.h"

#include <stdlib.h>
#include <algorithm>

static const efftype_id effect_cold( "cold" );
static const efftype_id effect_hot( "hot" );
static const efftype_id effect_took_prozac( "took_prozac" );

namespace
{
static const std::string item_name_placeholder = "%i"; // Used to address an item name

const std::string &get_morale_data( const morale_type id )
{
    static const std::array<std::string, NUM_MORALE_TYPES> morale_data = { {
            { "This is a bug (player.cpp:moraledata)" },
            { _( "Enjoyed %i" ) },
            { _( "Enjoyed a hot meal" ) },
            { _( "Music" ) },
            { _( "Enjoyed honey" ) },
            { _( "Played Video Game" ) },
            { _( "Marloss Bliss" ) },
            { _( "Mutagenic Anticipation" ) },
            { _( "Good Feeling" ) },
            { _( "Supported" ) },
            { _( "Looked at photos" ) },

            { _( "Nicotine Craving" ) },
            { _( "Caffeine Craving" ) },
            { _( "Alcohol Craving" ) },
            { _( "Opiate Craving" ) },
            { _( "Speed Craving" ) },
            { _( "Cocaine Craving" ) },
            { _( "Crack Cocaine Craving" ) },
            { _( "Mutagen Craving" ) },
            { _( "Diazepam Craving" ) },
            { _( "Marloss Craving" ) },

            { _( "Disliked %i" ) },
            { _( "Ate Human Flesh" ) },
            { _( "Ate Meat" ) },
            { _( "Ate Vegetables" ) },
            { _( "Ate Fruit" ) },
            { _( "Lactose Intolerance" ) },
            { _( "Ate Junk Food" ) },
            { _( "Wheat Allergy" ) },
            { _( "Ate Indigestible Food" ) },
            { _( "Wet" ) },
            { _( "Dried Off" ) },
            { _( "Cold" ) },
            { _( "Hot" ) },
            { _( "Bad Feeling" ) },
            { _( "Killed Innocent" ) },
            { _( "Killed Friend" ) },
            { _( "Guilty about Killing" ) },
            { _( "Guilty about Mutilating Corpse" ) },
            { _( "Fey Mutation" ) },
            { _( "Chimerical Mutation" ) },
            { _( "Mutation" ) },

            { _( "Moodswing" ) },
            { _( "Read %i" ) },
            { _( "Got comfy" ) },

            { _( "Heard Disturbing Scream" ) },

            { _( "Masochism" ) },
            { _( "Hoarder" ) },
            { _( "Stylish" ) },
            { _( "Optimist" ) },
            { _( "Bad Tempered" ) },
            //~ You really don't like wearing the Uncomfy Gear
            { _( "Uncomfy Gear" ) },
            { _( "Found kitten <3" ) },

            { _( "Got a Haircut" ) },
            { _( "Freshly Shaven" ) },
        }
    };

    if( static_cast<size_t>( id ) >= morale_data.size() ) {
        debugmsg( "invalid morale type: %d", id );
        return morale_data[0];
    }

    return morale_data[id];
}
} // namespace

// Morale multiplier
struct morale_mult {
    morale_mult(): good( 1.0 ), bad( 1.0 ) {}
    morale_mult( double good, double bad ): good( good ), bad( bad ) {}
    morale_mult( double both ): good( both ), bad( both ) {}

    double good;    // For good morale
    double bad;     // For bad morale

    morale_mult operator * ( const morale_mult &rhs ) const {
        return morale_mult( *this ) *= rhs;
    }

    morale_mult &operator *= ( const morale_mult &rhs ) {
        good *= rhs.good;
        bad *= rhs.bad;
        return *this;
    }
};

inline double operator * ( double morale, const morale_mult &mult )
{
    return morale * ( ( morale >= 0.0 ) ? mult.good : mult.bad );
}

inline double operator * ( const morale_mult &mult, double morale )
{
    return morale * mult;
}

inline double operator *= ( double &morale, const morale_mult &mult )
{
    morale = morale * mult;
    return morale;
}

inline int operator *= ( int &morale, const morale_mult &mult )
{
    morale = morale * mult;
    return morale;
}

// Commonly used morale multipliers
namespace morale_mults
{
// Optimistic characters focus on the good things in life,
// and downplay the bad things.
static const morale_mult optimist( 1.25, 0.75 );
// Again, those grouchy Bad-Tempered folks always focus on the negative.
// They can't handle positive things as well.  They're No Fun.  D:
static const morale_mult badtemper( 0.75, 1.25 );
// Prozac reduces overall negative morale by 75%.
static const morale_mult prozac( 1.0, 0.25 );
}

std::string player_morale::morale_point::get_name() const
{
    std::string name = get_morale_data( type );

    if( item_type != nullptr ) {
        name = string_replace( name, item_name_placeholder, item_type->nname( 1 ) );
    } else if( name.find( item_name_placeholder ) != std::string::npos ) {
        debugmsg( "Morale #%d (%s) requires item_type to be specified.", type,
                  name.c_str() );
    }

    return name;
}

int player_morale::morale_point::get_net_bonus() const
{
    return bonus * ( ( !is_permanent() && age > decay_start ) ?
                     logarithmic_range( decay_start, duration, age ) : 1 );
}

int player_morale::morale_point::get_net_bonus( const morale_mult &mult ) const
{
    return get_net_bonus() * mult;
}

bool player_morale::morale_point::is_expired() const
{
    // Zero morale bonuses will be shown occasionally anyway
    return ( !is_permanent() && age >= duration ) || bonus == 0;
}

bool player_morale::morale_point::is_permanent() const
{
    return ( duration == 0 );
}

bool player_morale::morale_point::matches( morale_type _type, const itype *_item_type ) const
{
    return ( type == _type ) && ( item_type == _item_type );
}

void player_morale::morale_point::add( int new_bonus, int new_max_bonus, int new_duration,
                                       int new_decay_start,
                                       bool new_cap )
{
    new_duration = std::max( 0, new_duration );

    if( new_cap || new_duration == 0 ) {
        duration = new_duration;
        decay_start = new_decay_start;
    } else {
        bool same_sign = ( bonus > 0 ) == ( new_max_bonus > 0 );

        duration = pick_time( duration, new_duration, same_sign );
        decay_start = pick_time( decay_start, new_decay_start, same_sign );
    }

    bonus = get_net_bonus() + new_bonus;

    if( abs( bonus ) > abs( new_max_bonus ) && ( new_max_bonus != 0 || new_cap ) ) {
        bonus = new_max_bonus;
    }

    age = 0; // Brand new. The assignment should stay below get_net_bonus() and pick_time().
}

int player_morale::morale_point::pick_time( int current_time, int new_time, bool same_sign ) const
{
    const int remaining_time = current_time - age;
    return ( remaining_time <= new_time && same_sign ) ? new_time : remaining_time;
}

void player_morale::morale_point::decay( int ticks )
{
    if( ticks < 0 ) {
        debugmsg( "The function called with negative ticks %d.", ticks );
        return;
    }

    age += ticks;
}

void player_morale::add( morale_type type, int bonus, int max_bonus,
                         int duration, int decay_start,
                         bool capped, const itype *item_type )
{
    for( auto &m : points ) {
        if( m.matches( type, item_type ) ) {
            const int prev_bonus = m.get_net_bonus();

            m.add( bonus, max_bonus, duration, decay_start, capped );

            if( m.is_expired() ) {
                remove_expired();
            } else if( m.get_net_bonus() != prev_bonus ) {
                invalidate();
            }

            return;
        }
    }

    morale_point new_morale( type, item_type, bonus, duration, decay_start );

    if( !new_morale.is_expired() ) {
        points.push_back( new_morale );
        invalidate();
    }
}

void player_morale::add_permanent( morale_type type, int bonus, int max_bonus, bool capped,
                                   const itype *item_type )
{
    add( type, bonus, max_bonus, 0, 0, capped, item_type );
}

int player_morale::has( morale_type type, const itype *item_type ) const
{
    for( auto &m : points ) {
        if( m.matches( type, item_type ) ) {
            return m.get_net_bonus();
        }
    }
    return 0;
}

void player_morale::remove_if( const std::function<bool( const morale_point & )> &func )
{
    const auto new_end = std::remove_if( points.begin(), points.end(), func );

    if( new_end != points.end() ) {
        points.erase( new_end, points.end() );
        invalidate();
    }
}

void player_morale::remove( morale_type type, const itype *item_type )
{
    remove_if( [ type, item_type ]( const morale_point & m ) -> bool {
        return m.matches( type, item_type );
    } );

}

void player_morale::remove_expired()
{
    remove_if( []( const morale_point & m ) -> bool {
        return m.is_expired();
    } );
}

morale_mult player_morale::get_temper_mult() const
{
    morale_mult mult;

    if( has( MORALE_PERM_OPTIMIST ) ) {
        mult *= morale_mults::optimist;
    }
    if( has( MORALE_PERM_BADTEMPER ) ) {
        mult *= morale_mults::badtemper;
    }

    return mult;
}

int player_morale::get_level() const
{
    if( !level_is_valid ) {
        const morale_mult mult = get_temper_mult();

        level = 0;
        for( auto &m : points ) {
            level += m.get_net_bonus( mult );
        }

        if( took_prozac ) {
            level *= morale_mults::prozac;
        }

        level_is_valid = true;
    }

    return level;
}

void player_morale::decay( int ticks )
{
    const auto do_decay = [ ticks ]( morale_point & m ) {
        m.decay( ticks );
    };

    std::for_each( points.begin(), points.end(), do_decay );
    remove_expired();

    for( int i = 0; i < ticks; i++ ) {
        update_bodytemp_penalty();
    }

    invalidate();
}

void player_morale::display( double focus_gain )
{
    const char *morale_gain_caption = _( "Total morale gain" );
    const char *focus_gain_caption = _( "Focus gain per minute" );

    // Figure out how wide the source column needs to be.
    int source_column_width = utf8_width( focus_gain_caption );
    if( utf8_width( morale_gain_caption ) > source_column_width ) {
        source_column_width = utf8_width( morale_gain_caption );
    }
    for( auto &i : points ) {
        const int length = utf8_width( i.get_name() );
        if( length > source_column_width ) {
            source_column_width = length;
        }
    }

    const int win_w = std::min( source_column_width + 4 + 8, FULL_SCREEN_WIDTH );
    const int win_h = FULL_SCREEN_HEIGHT;
    const int win_x = ( TERMX - win_w ) / 2;
    const int win_y = ( TERMY - win_h ) / 2;

    WINDOW *w = newwin( win_h, win_w, win_y, win_x );

    draw_border( w );

    mvwhline( w, 2, 0, LINE_XXXO, 1 );
    mvwhline( w, 2, 1, 0, win_w - 2 );
    mvwhline( w, 2, win_w - 1, LINE_XOXX, 1 );

    mvwhline( w, win_h - 4, 0, LINE_XXXO, 1 );
    mvwhline( w, win_h - 4, 1, 0, win_w - 2 );
    mvwhline( w, win_h - 4, win_w - 1, LINE_XOXX, 1 );

    mvwprintz( w, 1, 2, c_white, _( "Morale" ) );

    const auto get_value_color = []( double value ) -> nc_color {
        if( value > 0.0 )
        {
            return c_green;
        }
        if( value < 0.0 )
        {
            return c_red;
        }
        return c_dkgray;
    };

    if( points.size() > 0 ) {
        const char *source_column = _( "Source" );
        const char *value_column = _( "Value" );

        mvwprintz( w, 3,  2, c_ltgray, source_column );
        mvwprintz( w, 3, win_w - utf8_width( value_column ) - 2, c_ltgray, value_column );

        int line = 0;
        for( size_t i = 0; i < points.size(); ++i ) {
            static const morale_mult mult = get_temper_mult();

            const std::string name = points[i].get_name();
            const int bonus = points[i].get_net_bonus( mult );
            const nc_color bonus_color = get_value_color( bonus );

            if( bonus != 0 ) { // @todo Zero bonuses should be removed from the list
                mvwprintz( w, line + 4, win_w - 8, bonus_color, "%+6d", bonus );
            } else {
                mvwprintz( w, line + 4, win_w - 3, bonus_color, "-" );
            }

            line += fold_and_print_from( w, line + 4, 2, win_w - 9, 0, bonus_color, name.c_str() );

            if( line >= win_h - 8 ) {
                break;  // This prevents overflowing (unlikely, but just in case)
            }
        }
    } else {
        fold_and_print_from( w, 3, 2, win_w - 4, 0, c_dkgray, _( "Nothing affects your morale" ) );
    }

    const nc_color level_color = get_value_color( get_level() );
    const nc_color gain_color = get_value_color( focus_gain );

    mvwprintz( w, win_h - 3, 2, level_color, morale_gain_caption );
    mvwprintz( w, win_h - 2, 2, gain_color, focus_gain_caption );

    if( get_level() != 0 ) {
        mvwprintz( w, win_h - 3, win_w - 8, level_color, "%+6d", get_level() );
    } else {
        mvwprintz( w, win_h - 3, win_w - 3, level_color, "-" );
    }

    if( focus_gain != 0.0 ) {
        mvwprintz( w, win_h - 2, win_w - 8, gain_color, "%+6.2f", focus_gain );
    } else {
        mvwprintz( w, win_h - 2, win_w - 3, gain_color, "-" );
    }

    wrefresh( w );

    getch();

    werase( w );
    delwin( w );
}

void player_morale::clear()
{
    points.clear();
    covered.fill( 0 );
    cold.fill( 0 );
    hot.fill( 0 );
    took_prozac = false;
    stylish = false;
    super_fancy_bonus = 0;

    invalidate();
}

void player_morale::invalidate()
{
    level_is_valid = false;
}

void player_morale::on_mutation_gain( const std::string &mid )
{
    if( mid == "OPTIMISTIC" ) {
        add_permanent( MORALE_PERM_OPTIMIST, 4, 4 );
    } else if( mid == "BADTEMPER" ) {
        add_permanent( MORALE_PERM_BADTEMPER, -4, -4 );
    } else if( mid == "STYLISH" ) {
        set_stylish( true );
    }
}

void player_morale::on_mutation_loss( const std::string &mid )
{
    if( mid == "OPTIMISTIC" ) {
        remove( MORALE_PERM_OPTIMIST );
    } else if( mid == "BADTEMPER" ) {
        remove( MORALE_PERM_BADTEMPER );
    } else if( mid == "STYLISH" ) {
        set_stylish( false );
    }
}

void player_morale::on_item_wear( const item &it )
{
    set_worn( it, true );
}

void player_morale::on_item_takeoff( const item &it )
{
    set_worn( it, false );
}

void player_morale::on_effect_int_change( const efftype_id &eid, int intensity, body_part bp )
{
    if( eid == effect_took_prozac && bp == num_bp ) {
        set_prozac( intensity != 0 );
    } else if( eid == effect_cold && bp < num_bp ) {
        cold[bp] = intensity;
    } else if( eid == effect_hot && bp < num_bp ) {
        hot[bp] = intensity;
    }
}

void player_morale::set_worn( const item &it, bool worn )
{
    const bool just_fancy = it.has_flag( "FANCY" );
    const bool super_fancy = it.has_flag( "SUPER_FANCY" );

    if( just_fancy || super_fancy ) {
        const int sign = ( worn ) ? 1 : -1;

        for( int i = 0; i < num_bp; i++ ) {
            const auto bp = static_cast<body_part>( i );
            if( it.covers( bp ) ) {
                covered[i] = std::max( covered[i] + sign, 0 );
            }
        }

        if( super_fancy ) {
            super_fancy_bonus += 2 * sign;
        }

        update_stylish_bonus();
    }
}

void player_morale::set_prozac( bool new_took_prozac )
{
    if( took_prozac != new_took_prozac ) {
        took_prozac = new_took_prozac;
        invalidate();
    }
}

void player_morale::set_stylish( bool new_stylish )
{
    if( stylish != new_stylish ) {
        stylish = new_stylish;
        update_stylish_bonus();
    }
}

void player_morale::update_stylish_bonus()
{
    int bonus = 0;

    if( stylish ) {
        if( covered[bp_torso] ) {
            bonus += 6;
        }
        if( covered[bp_head] ) {
            bonus += 3;
        }
        if( covered[bp_eyes] ) {
            bonus += 2;
        }
        if( covered[bp_mouth] ) {
            bonus += 2;
        }
        if( covered[bp_leg_l] || covered[bp_leg_r] ) {
            bonus += 2;
        }
        if( covered[bp_foot_l] || covered[bp_foot_r] ) {
            bonus += 1;
        }
        if( covered[bp_hand_l] || covered[bp_hand_r] ) {
            bonus += 1;
        }

        bonus = std::min( bonus + super_fancy_bonus, 20 );
    }

    add_permanent( MORALE_PERM_FANCY, bonus, bonus, true );
}

void player_morale::update_bodytemp_penalty()
{
    const auto bp_pen = [ this ]( body_part bp, double mul ) -> int {
        return mul * ( hot[bp] - cold[bp] );
    };

    const int pen =
        bp_pen( bp_head,    2 ) +
        bp_pen( bp_torso,   2 ) +
        bp_pen( bp_mouth,   2 ) +
        bp_pen( bp_arm_l,  .5 ) +
        bp_pen( bp_arm_r,  .5 ) +
        bp_pen( bp_leg_l,  .5 ) +
        bp_pen( bp_leg_r,  .5 ) +
        bp_pen( bp_hand_l, .5 ) +
        bp_pen( bp_hand_r, .5 ) +
        bp_pen( bp_foot_l, .5 ) +
        bp_pen( bp_foot_r, .5 );

    if( pen < 0 ) {
        add( MORALE_COLD, -2, pen, 10, 5, true );
    } else if( pen > 0 ) {
        add( MORALE_HOT, -2, -pen, 10, 5, true );
    }
}
