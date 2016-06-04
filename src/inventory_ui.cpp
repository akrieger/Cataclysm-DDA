#include "game.h"
#include "player.h"
#include "action.h"
#include "map.h"
#include "output.h"
#include "uistate.h"
#include "translations.h"
#include "options.h"
#include "messages.h"
#include "input.h"
#include "catacharset.h"
#include "item_location.h"
#include "vehicle.h"
#include "vehicle_selector.h"
#include "cata_utility.h"
#include "itype.h"

#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <algorithm>

/**
 * - it != NULL -> item entry, print the item name and allow selecting it, or
 * - it == NULL && category != NULL -> category header, print it specially,
 * do not allow selecting it, or
 * - it == NULL && category == NULL -> empty entry, do not display at all.
 * It is used as the last entry on a page in case the next entry would be
 * a category header.
 */
struct itemstack_or_category {
    /** The item that should be displayed here. Can be NULL. */
    const item *it;
    /** Pointer into an inventory slice, can be NULL, if not NULL, it should not
     * point to an empty list. The first entry should be the same as @ref it. */
    const std::list<item> *slice;
    /** The category of an item. */
    const item_category *category;
    /** The item position in the players inventory. It should be unique as it
     * is used by the drop map in inventory_selector. */
    int item_pos;
    itemstack_or_category(const indexed_invslice::value_type &a)
        : it(&(a.first->front())), slice(a.first), category(&it->get_category()), item_pos(a.second)
    {
    }
    itemstack_or_category(const item *a, int b)
        : it(a), slice(NULL), category(&it->get_category()), item_pos(b)
    {
    }
    itemstack_or_category(const item_category *a = NULL)
        : it(NULL), slice(NULL), category(a), item_pos(INT_MIN)
    {
    }
    // used for searching the category header, only the item pointer and the category are important there
    bool operator==(const itemstack_or_category &other) const
    {
        return category == other.category && it == other.it;
    }
};

enum class navigation_mode : int {
    ITEM = 0,
    CATEGORY
};

enum class add_to : int {
    BEGINNING = 0,
    END
};

class inventory_column {
    public:
        static const size_t npos = -1; // Returned if no item found

        std::vector<itemstack_or_category> items;

        inventory_column() :
            items(),
            mode( navigation_mode::ITEM ),
            selected_index( 1 ),
            page_offset( 0 ),
            items_per_page( 1 ) {}

        virtual ~inventory_column() {}

        bool empty() const {
            return items.empty();
        }
        // true if can be shown on the screen
        virtual bool visible() const {
            return !empty();
        }
        // true if can be activated
        virtual bool activatable() const {
            return visible();
        }
        // true if allows selecting items
        virtual bool allows_selecting() const {
            return visible();
        }

        bool is_category( size_t index ) const {
            return items[index].it == nullptr;
        }

        size_t page_index() const {
            return page_offset / items_per_page;
        }

        size_t pages_count() const {
            return ( items.size() + items_per_page - 1 ) / items_per_page;
        }

        virtual void set_mode( navigation_mode new_mode ) {
            mode = new_mode;
        }

        size_t find_if( const std::function<bool( const itemstack_or_category & )> &pred ) const;
        size_t find_by_pos( int pos ) const;
        size_t find_by_invlet( long invlet ) const;
        size_t get_max_width() const;

        void draw( WINDOW *win, size_t x, size_t y, size_t width, bool active,
                   const std::function<char( const itemstack_or_category &entry )> &get_icon ) const;

        void select( size_t new_index );
        std::vector<int> get_selected() const;

        void add_item( const itemstack_or_category &item_entry, const itemstack_or_category &cat_entry, add_to where );
        void add_items( const indexed_invslice &slice, add_to where, const item_category *def_cat = nullptr );
        void remove_item( const itemstack_or_category &entry );
        void prepare_paging( size_t items_per_page );

        virtual void on_action( const std::string &action );
        virtual void on_item_include( const item &, int ) {}
        virtual void on_item_exclude( const item &, int ) {}

    protected:
        virtual std::string get_item_text( size_t index, const char icon = '\0' ) const;
        virtual nc_color get_item_color( size_t index, bool active ) const;
        virtual size_t get_item_indent( size_t index ) const {
            return is_category( index ) ? 0 : 2;
        }

        navigation_mode mode;
        size_t selected_index;
        size_t page_offset;
        size_t items_per_page;
};

class selection_column : public inventory_column {
    public:
        selection_column( ) :
            inventory_column(),
            selected_cat( "ITEMS YOU SELECTED", _( "ITEMS YOU SELECTED:" ), 0 ) {

            items.push_back( itemstack_or_category( &selected_cat ) );
        }

        virtual bool activatable() const override {
            // The column can be activated if there's more than one page to scroll it
            return inventory_column::activatable() && pages_count() > 1;
        }

        virtual bool allows_selecting() const override {
            return false;
        }

        virtual void set_mode( navigation_mode ) override {
            // ignore assignment of a non-default mode
        }

        virtual void on_item_include( const item &it, int pos ) override;
        virtual void on_item_exclude( const item &it, int pos ) override;

    protected:
        virtual std::string get_item_text( size_t index, const char icon = '\0' ) const override;
        virtual nc_color get_item_color( size_t index, bool ) const override;

    private:
        const item_category selected_cat;
};

const item_filter allow_all_items = []( const item & ) { return true; };

class inventory_selector
{
    public:
        /**
         * Extracts <B>slice</B> into @ref items, adding category entries.
         * For each item in the slice an entry that points to it is added to @ref items.
         * For a consecutive sequence of items of the same category a single
         * category entry is added in front of them or after them depending on @ref where.
         */
        void add_items( const indexed_invslice &slice, add_to where, const item_category *def_cat = nullptr );
        /**
         * Checks the selector for emptiness (absence of available items).
         */
        bool empty() const {
            for( const auto column : columns ) {
                if( !column->empty() ) {
                    return false;
                }
            }
            return true;
        }
        /** Creates the inventory screen */
        inventory_selector( player &u, item_filter filter = allow_all_items );
        ~inventory_selector();

        /** Executes the selector */
        int execute_pick( const std::string &title, const int position = INT_MIN );
        // @todo opts should not be passed here. Temporary solution for further refactoring
        item_location execute_pick_map( const std::string &title, std::unordered_map<item *, item_location> &opts );
        void execute_compare( const std::string &title );
        std::list<std::pair<int, int>> execute_multidrop( const std::string &title );

    private:
        typedef std::vector<itemstack_or_category> itemstack_vector;

        std::vector<std::shared_ptr<inventory_column>> columns;
        size_t column_index;

        enum selector_mode{
            SM_PICK,
            SM_COMPARE,
            SM_MULTIDROP
        };
        /**
         * Inserts additional category entries on top of each page,
         * When the last entry of a page is a category entry, inserts an empty entry
         * right before that one. The category entry goes now on the next page.
         * This is done for both list (@ref items and @ref worn).
         */
        void prepare_paging();
        /**
         * What has been selected for dropping/comparing. The key is the item position,
         * the value is the count, or -1 for dropping all. The class makes sure that
         * the count is never 0, and it is -1 only if all items should be dropped.
         * Any value > 0 means at least one item will remain after dropping.
         */
        typedef std::map<int, int> drop_map;
        drop_map dropping;
        /** when comparing: the first item to be compared, or NULL */
        item *first_item;
        /** when comparing: the second item or NULL */
        item *second_item;
        /** The input context for navigation, already contains some actions for movement.
         * See @ref handle_movement */
        input_context ctxt;
        /** Given an action from the input_context, try to act according to it:
         * move selection around (next/previous page/item).
         * If not handle by this class it return false, otherwise true (caller should
         * ignore the action in this case). */
        void handle_movement( const std::string &action );
        /** Update the @ref w_inv window, including wrefresh */
        void display( const std::string &title, selector_mode mode ) const;
        /** Returns the item positions of the currently selected entry, or ITEM_MIN
         * if no entry is selected. */
        int get_selected_item_position() const;
        /** Set/toggle dropping count items of currently selected item stack, see @ref set_drop_count */
        void set_selected_to_drop(int count);
        /** Select the item at position and set the correct in_inventory and current_page_offset value */
        void select_item_by_position(const int &position);
        void remove_dropping_items( player &u ) const;
        WINDOW *w_inv;

        navigation_mode navigation;

        const item_category weapon_cat;
        const item_category worn_cat;

        player &u;

        void print_inv_weight_vol(int weight_carried, int vol_carried, int vol_capacity) const;

        /** Returns an entry from @ref items by its invlet */
        const itemstack_or_category *invlet_to_itemstack( long invlet ) const;
        /** Toggle item dropping for item position it_pos:
         * If count is > 0: set dropping to count
         * If the item is already marked for dropping: deactivate dropping,
         * If the item is not marked for dropping: set dropping to -1
         * The item reference is used to update @ref first_item / @ref second_item
         */
        void set_drop_count(int it_pos, int count, const item &it);
        /**
         * Same as @ref set_drop_count with single item,
         * if count is > 0: set count to -1 if it reaches/exceeds the maximal
         * droppable items of this stack (if stack.size() == 4 and count == 4, set
         * count to -1 because that means drop all).
         */
        void set_drop_count(int it_pos, int count, const std::list<item> &stack);
        void set_to_drop(int it_pos, int count);
        void toggle_active_column();
        void toggle_navigation_mode();

        void add_column( inventory_column *new_column );
};

size_t inventory_column::find_if( const std::function<bool( const itemstack_or_category & )> &pred ) const
{
    for( size_t i = 0; i < items.size(); ++i ) {
        if( pred( items[i] ) ) {
            return i;
        }
    }
    return npos;
}

size_t inventory_column::find_by_pos( int pos ) const
{
    return find_if( [ pos ]( const itemstack_or_category &it ) {
        return it.item_pos == pos;
    } );
}

size_t inventory_column::find_by_invlet( long invlet ) const
{
    return find_if( [ invlet ]( const itemstack_or_category &it ) {
        return it.it != nullptr && it.it->invlet == invlet;
    } );
}

size_t inventory_column::get_max_width() const
{
    size_t res = 0;
    for( size_t index = 0; index < items.size(); ++index ) {
        const size_t text_length = utf8_width( get_item_text( index ) );
        res = std::max( res, get_item_indent( index ) + text_length );
    }
    return res;
}

void inventory_column::select( size_t new_index )
{
    if( new_index != selected_index && new_index < items.size() ) {
        selected_index = new_index;
        page_offset = selected_index - selected_index % items_per_page;
    }
}

std::vector<int> inventory_column::get_selected() const
{
    std::vector<int> res;

    if( !allows_selecting() || selected_index >= items.size() ) {
        return res;
    }

    switch( mode ) {
        case navigation_mode::ITEM:
            if( !is_category( selected_index ) ) {
                res.push_back( selected_index );
            }
            break;
        case navigation_mode::CATEGORY:
            const auto cur_cat = items[selected_index].category;
            for( size_t i = 0; i < items.size(); ++i ) {
                if( !is_category( i ) && items[i].category == cur_cat ) {
                    res.push_back( i );
                }
            }
            break;
    }

    return res;
}

void inventory_column::on_action( const std::string &action )
{
    const auto is_unsuitable = [ this ]( size_t index ) {
        return is_category( index ) || ( mode == navigation_mode::CATEGORY &&
                                         items[index].category == items[selected_index].category );
    };

    const auto move_forward = [ this, &is_unsuitable ]( size_t step = 1 ) {
        size_t index = ( selected_index + step < items.size() ) ? selected_index + step : 0;
        while( is_unsuitable( index ) ) {
            if( ++index == items.size() ) {
                index = 0;
            } else if( index == selected_index ) {
                return; // no movement is possible
            }
        }
        select( index );
    };

    const auto move_backward = [ this, &is_unsuitable ]( size_t step = 1 ) {
        size_t index = ( selected_index >= step ) ? selected_index - step : items.size() - 1;
        while( is_unsuitable( index ) ) {
            if( index == 0 ) {
                index = items.size() - 1;
            } else if( --index == selected_index ) {
                return; // no movement is possible
            }
        }
        select( index );
    };

    if( action == "DOWN" ) {
        move_forward();
    } else if( action == "UP" ) {
        move_backward();
    } else if( action == "NEXT_TAB" ) {
        move_forward( items_per_page );
    } else if( action == "PREV_TAB" ) {
        move_backward( items_per_page );
    } else if( action == "HOME" ) {
        select( 1 );
    } else if( action == "END" ) {
        select( items.size() - 1 );
    }
}

void inventory_column::add_item( const itemstack_or_category &item_entry, const itemstack_or_category &cat_entry, add_to where )
{
    auto cat_iter = std::find( items.begin(), items.end(), cat_entry );

    if( cat_iter == items.end() ) {
        // Category is not yet contained in the list, add it
        switch ( where ) {
            case add_to::BEGINNING:
                cat_iter = ++items.insert( items.begin(), cat_entry );
                break;
            case add_to::END:
                items.push_back( cat_entry );
                cat_iter = items.end();
                break;
        }
    } else {
        // Category is already contained, skip all the items that belong to the
        // category to insert the current item behind them (but before the next category)
        do {
            ++cat_iter;
        } while( cat_iter != items.end() && cat_iter->it != nullptr );
    }
    items.insert( cat_iter, item_entry );
}

void inventory_column::remove_item( const itemstack_or_category &entry )
{
    items.erase( std::remove( items.begin(), items.end(), entry ), items.end() );
    select( !items.empty() ? std::min( selected_index, items.size() - 1 ) : 0 );
}

void inventory_column::add_items( const indexed_invslice &slice, add_to where, const item_category *def_cat )
{
    for( const auto &scit : slice ) {
        const itemstack_or_category item_entry( scit );
        const itemstack_or_category cat_entry( def_cat != nullptr ? def_cat : item_entry.category );

        add_item( item_entry, cat_entry, where );
    }
}

void inventory_column::prepare_paging( size_t items_per_page )
{
    this->items_per_page = items_per_page;
    for( size_t i = 0; i < items.size(); ++i ) {
        const bool first_on_page = i % items_per_page == 0;
        const bool last_on_page = i % items_per_page == items_per_page - 1;

        if( last_on_page && is_category( i ) ) {
            items.insert( items.begin() + i, itemstack_or_category() );
        } else if( first_on_page && !is_category( i ) ) {
            items.insert( items.begin() + i, itemstack_or_category( items[i].category ) );
        }
    }
}

std::string inventory_column::get_item_text( size_t index, const char icon ) const
{
    std::ostringstream res;
    const item *it = items[index].it;

    if( it != nullptr ) {
        const size_t count = ( items[index].slice != nullptr ) ? items[index].slice->size() : 1;

        if( OPTIONS["ITEM_SYMBOLS"] ) {
            res << it->symbol() << ' ';
        }
        if( icon != '\0' ) {
            res << icon << ' ';
        }
        if( count > 1 ) {
            res << count << ' ';
        }
        res << it->display_name( count );
    } else if( items[index].category != nullptr ) {
        res << items[index].category->name;
    }
    return res.str();
}

nc_color inventory_column::get_item_color( size_t index, bool active ) const
{
    if( is_category( index ) ) {
        return c_magenta;
    }

    switch( mode ) {
        case navigation_mode::ITEM:
            if( index == selected_index ) {
                return active ? h_white : h_ltgray;
            }
            break;
        case navigation_mode::CATEGORY:
            if( items[index].category == items[selected_index].category ) {
                return active ? c_white_red : c_ltgray_red;
            }
            break;
    }

    return items[index].it->color_in_inventory();
}

void inventory_column::draw( WINDOW *win, size_t x, size_t y, size_t width, bool active,
                             const std::function<char( const itemstack_or_category &entry )> &get_icon ) const
{
    for( size_t i = page_offset, line = 0; i < items.size() && line < items_per_page; ++i, ++line ) {
        if( items[i].category == nullptr ) {
            continue;
        }

        if( !is_category( i ) ) {
            const char invlet = items[i].it->invlet;
            if( invlet != '\0' ) {
                const nc_color invlet_color = ( i == selected_index )
                    ? get_item_color( i, active )
                    : g->u.assigned_invlet.count( invlet )
                        ? c_yellow
                        : c_white;
                mvwputch( win, y + line, x, invlet_color, invlet );
            }
        }

        trim_and_print( win, y + line, x + get_item_indent( i ), width,
                        get_item_color( i, active ), "%s", get_item_text( i, get_icon( items[i] ) ).c_str() );
    }
}

void selection_column::on_item_include( const item &it, int pos )
{
    add_item( itemstack_or_category( &it, pos ),
              itemstack_or_category( &selected_cat ), add_to::END );
}

void selection_column::on_item_exclude( const item &it, int pos )
{
    remove_item( itemstack_or_category( &it, pos ) );
}

std::string selection_column::get_item_text( size_t index, const char icon ) const
{
    const std::string inherited( inventory_column::get_item_text( index, icon ) );
    if( is_category( index ) && items[index].category == &selected_cat ) {
        return string_format( "%s (%d)", inherited.c_str(), items.size() - 1 );
    }
    return inherited;
}

nc_color selection_column::get_item_color( size_t index, bool active ) const
{
    if( is_category( index ) || ( activatable() && active && index == selected_index ) ) {
        return inventory_column::get_item_color( index, active );
    }
    const int pos = items[index].item_pos;
    if( pos == -1 ) {
        return c_ltblue;
    } else if( pos <= -2 ) {
        return c_cyan;
    }
    return items[index].it->color_in_inventory();
}

void inventory_selector::add_items(const indexed_invslice &slice, add_to where, const item_category *def_cat)
{
    columns.front()->add_items( slice, where, def_cat );
}

const itemstack_or_category *inventory_selector::invlet_to_itemstack( long invlet ) const
{
    for( const auto column : columns ) {
        const size_t index = column->find_by_invlet( invlet );

        if( index != inventory_column::npos ) {
            return &column->items[index];
        }
    }
    return nullptr;
}

void inventory_selector::prepare_paging()
{
    size_t items_per_page = getmaxy( w_inv ) - 5;
    for( auto column : columns ) {
        column->prepare_paging( items_per_page );
    }
    column_index = 0;
    if( !columns.empty() && !columns[column_index]->activatable() ) {
        toggle_active_column();
    }
}

void inventory_selector::print_inv_weight_vol(int weight_carried, int vol_carried,
        int vol_capacity) const
{
    // Print weight
    mvwprintw(w_inv, 0, 32, _("Weight (%s): "), weight_units());
    nc_color weight_color;
    if (weight_carried > u.weight_capacity()) {
        weight_color = c_red;
    } else {
        weight_color = c_ltgray;
    }
    wprintz(w_inv, weight_color, "%6.1f", convert_weight(weight_carried) + 0.05 ); // +0.05 to round up;
    wprintz(w_inv, c_ltgray, "/%-6.1f", convert_weight(u.weight_capacity()));

    // Print volume
    mvwprintw(w_inv, 0, 61, _("Volume: "));
    if (vol_carried > vol_capacity) {
        wprintz(w_inv, c_red, "%3d", vol_carried);
    } else {
        wprintz(w_inv, c_ltgray, "%3d", vol_carried);
    }
    wprintw(w_inv, "/%-3d", vol_capacity);
}

void inventory_selector::display( const std::string &title, selector_mode mode ) const
{
    werase(w_inv);
    mvwprintw(w_inv, 0, 0, title.c_str());

    const size_t max_x = size_t( getmaxx( w_inv ) );
    size_t visible_columns = 0;
    size_t column_x = 1;

    for( const auto column : columns ) {
        if( !column->visible() ) {
            continue;
        }
        column_x += column->get_max_width();
        if( column_x > max_x ) {
            break;
        }
        ++visible_columns;
    }
    const size_t column_width =
        ( visible_columns > 0 ) ? size_t( getmaxx( w_inv ) ) / visible_columns : 0;

    for( size_t i = 0, column_x = 1; i < columns.size() && column_x < max_x;
        ++i, column_x += column_width ) {

        const auto &column = *columns[i];

        if( !column.visible() ) {
            continue;
        }

        column.draw( w_inv, column_x, 2, column_width, i == column_index,
            [ this, mode ]( const itemstack_or_category &entry ) -> char {
                if( mode == SM_PICK ) {
                    return '\0';
                }
                const auto dit = dropping.find( entry.item_pos );

                if( dit == dropping.end() ) {
                    return '-';
                } else if( dit->second == -1 ) {
                    return '+';
                }
                return '#';
            } );

        if( column.pages_count() > 1 ) {
            mvwprintw( w_inv, getmaxy( w_inv ) - 2, column_x, _( "Page %d/%d" ),
                       column.page_index() + 1, column.pages_count() );
        }
    }

    if( mode == SM_PICK ) {
        mvwprintw(w_inv, 1, 61, _("Hotkeys:  %d/%d "), u.allocated_invlets().size(), inv_chars.size());
    }

    if (mode == SM_MULTIDROP) {
        // Make copy, remove to be dropped items from that
        // copy and let the copy recalculate the volume capacity
        // (can be affected by various traits).
        player tmp = u;
        // first round: remove weapon & worn items, start with larges worn index
        for( const auto &elem : dropping ) {
            if( elem.first == -1 && elem.second == -1 ) {
                tmp.remove_weapon();
            } else if( elem.first == -1 && elem.second != -1 ) {
                tmp.weapon.charges -= elem.second;
            } else if( elem.first < 0 ) {
                tmp.i_rem( elem.first );
            }
        }
        remove_dropping_items(tmp);
        print_inv_weight_vol(tmp.weight_carried(), tmp.volume_carried(), tmp.volume_capacity());
        mvwprintw(w_inv, 1, 0, _("To drop x items, type a number and then the item hotkey."));
    } else {
        print_inv_weight_vol(u.weight_carried(), u.volume_carried(), u.volume_capacity());
    }
    if( empty() ) {
        mvwprintw(w_inv, 2, 0, _( "Your inventory is empty." ) );
    }

    const std::string msg_str = ( navigation == navigation_mode::CATEGORY )
        ? _( "Category selection; [TAB] switches mode, arrows select." )
        : _( "Item selection; [TAB] switches mode, arrows select." );
    const nc_color msg_color = ( navigation == navigation_mode::CATEGORY ) ? c_white_red : h_white;
    center_print( w_inv, getmaxy( w_inv ) - 1, msg_color, msg_str.c_str() );

    wrefresh(w_inv);
}

inventory_selector::inventory_selector( player &u, item_filter filter )
    : columns()
    , dropping()
    , first_item(NULL)
    , second_item(NULL)
    , ctxt("INVENTORY")
    , w_inv( newwin( TERMY, TERMX, VIEW_OFFSET_Y, VIEW_OFFSET_X ) )
    , navigation( navigation_mode::ITEM )
    , weapon_cat("WEAPON", _("WEAPON:"), 0)
    , worn_cat("ITEMS WORN", _("ITEMS WORN:"), 0)
    , u(u)
{
    ctxt.register_action("DOWN", _("Next item"));
    ctxt.register_action("UP", _("Previous item"));
    ctxt.register_action("RIGHT", _("Confirm"));
    ctxt.register_action("LEFT", _("Switch inventory/worn"));
    ctxt.register_action("CONFIRM", _("Mark selected item"));
    ctxt.register_action("QUIT", _("Cancel"));
    ctxt.register_action("CATEGORY_SELECTION");
    ctxt.register_action("NEXT_TAB", _("Page down"));
    ctxt.register_action("PREV_TAB", _("Page up"));
    ctxt.register_action("HOME", _("Home"));
    ctxt.register_action("END", _("End"));
    ctxt.register_action("HELP_KEYBINDINGS");
    ctxt.register_action("ANY_INPUT"); // For invlets

    auto first_column = new inventory_column();
    auto second_column = new inventory_column();

    first_column->add_items( u.inv.indexed_slice_filter_by( filter ), add_to::END );

    if( u.is_armed() && filter( u.weapon ) ) {
        second_column->add_item( itemstack_or_category( &u.weapon, -1 ),
                                 itemstack_or_category( &weapon_cat ), add_to::END );
    }

    size_t i = 0;
    for( const auto &it : u.worn ) {
        if( filter( it ) ) {
            second_column->add_item( itemstack_or_category( &it, player::worn_position_to_index( i ) ),
                                     itemstack_or_category( &worn_cat ), add_to::END );
        }
        ++i;
    }

    add_column( first_column );
    add_column( second_column );
}

inventory_selector::~inventory_selector()
{
    if (w_inv != NULL) {
        werase(w_inv);
        delwin(w_inv);
    }
    g->refresh_all();
}

void inventory_selector::handle_movement(const std::string &action)
{
    if( action == "CATEGORY_SELECTION" ) {
        toggle_navigation_mode();
    } else if( action == "LEFT" ) {
        toggle_active_column();
    } else if( column_index < columns.size() ) {
        columns[column_index]->on_action( action );
    }
}

void inventory_selector::select_item_by_position( const int &position )
{
    if( position == INT_MIN ) {
        return;
    }
    for( size_t c = 0; c < columns.size(); ++c ) {
        const size_t index = columns[c]->find_by_pos( position );

        if( index != inventory_column::npos ) {
            column_index = c;
            columns[column_index]->select( index );
            return;
        }
    }
}

int inventory_selector::get_selected_item_position() const
{
    const auto selection( columns[column_index]->get_selected() );

    if( !selection.empty() ) {
        return columns[column_index]->items[selection.front()].item_pos;
    }
    return INT_MIN;
}

void inventory_selector::set_selected_to_drop(int count)
{
    if( column_index > columns.size() ) {
        return;
    }
    for( const int index : columns[column_index]->get_selected() ) {
        const itemstack_or_category &cur_entry = columns[column_index]->items[index];

        if( cur_entry.slice != nullptr ) {
            set_drop_count( cur_entry.item_pos, count, *cur_entry.slice );
        } else if( cur_entry.it != nullptr ) {
            if( count > 0 && ( !cur_entry.it->count_by_charges() || count >= cur_entry.it->charges ) ) {
                count = -1;
            }
            set_drop_count( cur_entry.item_pos, count, *cur_entry.it );
        }
    }
}

void inventory_selector::set_to_drop(int it_pos, int count)
{
    if (it_pos == -1) { // weapon
        if (u.weapon.is_null()) {
            return;
        }
        if (count > 0 && (!u.weapon.count_by_charges() || count >= u.weapon.charges)) {
            count = -1; // drop whole item, because it can not be separated, or the requested count means all
        }
        // Must bypass the set_drop_count() that takes a stack,
        // because it must get a direct reference to weapon.
        set_drop_count(it_pos, count, u.weapon);
    } else if (it_pos < -1) { // worn
        item& armor = u.i_at( it_pos );
        if( armor.is_null() ) {
            return; // invalid it_pos -> ignore
        }
        if (count > 0) {
            count = -1; // can only drop a whole worn item
        }
        set_drop_count(it_pos, count, armor);
    } else { // inventory
        const std::list<item> &stack = u.inv.const_stack(it_pos);
        if (stack.empty()) {
            return; // invalid it_pos -> ignore
        }
        set_drop_count(it_pos, count, stack);
    }
}

void inventory_selector::set_drop_count(int it_pos, int count, const std::list<item> &stack)
{
    if (stack.size() == 1) {
        const item &it = stack.front();
        if (count > 0 && (!it.count_by_charges() || count >= it.charges)) {
            count = -1; // drop whole item, because it can not be separated, or count is big enough anyway
        }
    } else if (count > 0 && (size_t) count >= stack.size()) {
        count = -1; // count indicates whole stack anyway
    } else if (stack.empty()) {
        return;
    }
    set_drop_count(it_pos, count, stack.front());
}

void inventory_selector::set_drop_count(int it_pos, int count, const item &it)
{
    // count 0 means toggle, if already selected for dropping, drop none
    drop_map::iterator iit = dropping.find(it_pos);
    if (count == 0 && iit != dropping.end()) {
        dropping.erase(iit);
        if (first_item == &it) {
            first_item = second_item;
            second_item = NULL;
        } else if (second_item == &it) {
            second_item = NULL;
        }
        for( auto column : columns ) {
            column->on_item_exclude( it, it_pos );
        }
    } else {
        // allow only -1 or anything > 0
        dropping[it_pos] = (count <= 0) ? -1 : count;
        if (first_item == NULL || first_item == &it) {
            first_item = const_cast<item *>(&it);
        } else {
            second_item = const_cast<item *>(&it);
        }
        for( auto column : columns ) {
            column->on_item_include( it, it_pos );
        }
    }
}

void inventory_selector::remove_dropping_items( player &u ) const
{
    // We iterate backwards because deletion will invalidate later indices.
    for( inventory_selector::drop_map::const_reverse_iterator a = dropping.rbegin();
         a != dropping.rend(); ++a ) {
        if( a->first < 0 ) { // weapon or armor, handled separately
            continue;
        }
        const int count = a->second;
        item &tmpit = u.inv.find_item( a->first );
        if( tmpit.count_by_charges() ) {
            long charges = tmpit.charges;
            if( count != -1 && count < charges ) {
                tmpit.charges -= count;
            } else {
                u.inv.remove_item( a->first );
            }
        } else {
            size_t max_count = u.inv.const_stack( a->first ).size();
            if( count != -1 && ( size_t )count < max_count ) {
                max_count = count;
            }
            for( size_t i = 0; i < max_count; i++ ) {
                u.inv.remove_item( a->first );
            }
        }
    }
}

void inventory_selector::toggle_active_column()
{
    const size_t prev_index = column_index;
    do {
        if( ++column_index == columns.size() ) {
            column_index = 0;
        }
    } while( column_index != prev_index && !columns[column_index]->activatable() );
}

void inventory_selector::toggle_navigation_mode()
{
    switch( navigation ) {
        case navigation_mode::ITEM:
            navigation = navigation_mode::CATEGORY;
            break;
        case navigation_mode::CATEGORY:
            navigation = navigation_mode::ITEM;
            break;
    }
    for( auto column : columns ) {
        column->set_mode( navigation );
    }
}

void inventory_selector::add_column( inventory_column *new_column )
{
    if( new_column != nullptr ) {
        std::shared_ptr<inventory_column> column( new_column );

        column->set_mode( navigation );
        columns.push_back( column );
    }
}

int inventory_selector::execute_pick( const std::string &title, const int position )
{
    prepare_paging();
    select_item_by_position( position );

    while( true ) {
        display( title, SM_PICK );

        const std::string action = ctxt.handle_input();
        const long ch = ctxt.get_raw_input().get_first_input();
        const auto itemstack = invlet_to_itemstack( ch );

        if( itemstack != nullptr ) {
            return itemstack->item_pos;
        } else if ( action == "CONFIRM" || action == "RIGHT" ) {
            const auto selection( columns[column_index]->get_selected() );

            if( selection.empty() ) {
                return INT_MIN;
            } else if( selection.size() == 1 ) {
                return columns[column_index]->items[selection.front()].item_pos;
            } else {
                const std::string msg( _( "Please pick only one item." ) );
                popup( msg , PF_GET_KEY );
            }
        } else if ( action == "QUIT" ) {
            return INT_MIN;
        } else {
            handle_movement( action );
        }
    }
}

item_location inventory_selector::execute_pick_map( const std::string &title, std::unordered_map<item *, item_location> &opts )
{
    prepare_paging();

    while( true ) {
        display( title, SM_PICK );

        const std::string action = ctxt.handle_input();
        const long ch = ctxt.get_raw_input().get_first_input();
        const auto itemstack = invlet_to_itemstack( ch );

        if( itemstack != nullptr ) {
            const auto it = opts.find( const_cast<item*>( itemstack->it ) );
            if( it != opts.end() ) {
                return std::move( it->second );
            }
            set_to_drop( itemstack->item_pos, 0 );
            return item_location( u, first_item );
        } else if( action == "QUIT" ) {
            return item_location();

        } else if( action == "RIGHT" || action == "CONFIRM" ) {
            set_selected_to_drop( 0 );

            // Item in inventory
            if( get_selected_item_position() != INT_MIN ) {
                return item_location( u, first_item );
            }
            // Item on ground or in vehicle
            auto it = opts.find( first_item );
            if( it != opts.end() ) {
                return std::move( it->second );
            }
            return item_location();
        } else {
            handle_movement( action );
        }
    }
}

void inventory_selector::execute_compare( const std::string &title )
{
    add_column( new selection_column() );
    prepare_paging();

    inventory_selector::drop_map prev_droppings;
    while(true) {
        display( title, SM_COMPARE );

        const std::string action = ctxt.handle_input();
        const long ch = ctxt.get_raw_input().get_first_input();
        const auto itemstack = invlet_to_itemstack( ch );

        if( itemstack != nullptr ) {
            set_drop_count( itemstack->item_pos, 0, *itemstack->it );
        } else if( action == "QUIT" ) {
            break;
        } else if(action == "RIGHT") {
            set_selected_to_drop( 0 );
        } else {
            handle_movement( action );
        }

        if (second_item != NULL) {
            std::vector<iteminfo> vItemLastCh, vItemCh;
            std::string sItemLastCh, sItemCh, sItemTn;
            first_item->info( true, vItemCh );
            sItemCh = first_item->tname();
            sItemTn = first_item->type_name();
            second_item->info(true, vItemLastCh);
            sItemLastCh = second_item->tname();

            int iScrollPos = 0;
            int iScrollPosLast = 0;
            int ch = ( int ) ' ';
            do {
                draw_item_info( 0, ( TERMX - VIEW_OFFSET_X * 2 ) / 2, 0, TERMY - VIEW_OFFSET_Y * 2,
                               sItemLastCh, sItemTn, vItemLastCh, vItemCh, iScrollPosLast, true ); //without getch()
                ch = draw_item_info( ( TERMX - VIEW_OFFSET_X * 2) / 2, (TERMX - VIEW_OFFSET_X * 2 ) / 2,
                                    0, TERMY - VIEW_OFFSET_Y * 2, sItemCh, sItemTn, vItemCh, vItemLastCh, iScrollPos );

                if( ch == KEY_PPAGE ) {
                    iScrollPos--;
                    iScrollPosLast--;
                } else if( ch == KEY_NPAGE ) {
                    iScrollPos++;
                    iScrollPosLast++;
                }
            } while ( ch == KEY_PPAGE || ch == KEY_NPAGE );

            dropping = prev_droppings;
            second_item = NULL;
        } else {
            prev_droppings = dropping;
        }
    }
}

std::list<std::pair<int, int>> inventory_selector::execute_multidrop( const std::string &title )
{
    add_column( new selection_column() );
    prepare_paging();

    int count = 0;
    while( true ) {
        display( title, SM_MULTIDROP );

        const std::string action = ctxt.handle_input();
        const long ch = ctxt.get_raw_input().get_first_input();
        const auto itemstack = invlet_to_itemstack( ch );

        if( ch >= '0' && ch <= '9' ) {
            count = std::min( count, INT_MAX / 10 - 10 );
            count *= 10;
            count += ch - '0';
        } else if( itemstack != nullptr ) {
            set_to_drop( itemstack->item_pos, count );
            count = 0;
        } else if( action == "RIGHT" ) {
            set_selected_to_drop( count );
            count = 0;
        } else if( action == "CONFIRM" ) {
            break;
        } else if( action == "QUIT" ) {
            return std::list<std::pair<int, int> >();
        } else {
            handle_movement( action );
            count = 0;
        }
    }

    std::list<std::pair<int, int>> dropped_pos_and_qty;

    for( auto drop_pair : dropping ) {
        int num_to_drop = drop_pair.second;
        if( num_to_drop == -1 ) {
            num_to_drop = inventory::num_items_at_position( drop_pair.first );
        }
        dropped_pos_and_qty.push_back( std::make_pair( drop_pair.first, num_to_drop ) );
    }

    return dropped_pos_and_qty;
}

// Display current inventory.
int game::inv( const int position )
{
    u.inv.restack( &u );
    u.inv.sort();

    return inventory_selector( u ).execute_pick( _( "Inventory:" ), position );
}

int game::inv_for_filter( const std::string &title, item_filter filter, const std::string &none_message )
{
    u.inv.restack( &u );
    u.inv.sort();

    inventory_selector inv_s( u, filter );

    if( inv_s.empty() ) {
        const std::string msg = ( none_message.empty() ) ? _( "You don't have the necessary item." ) : none_message;
        popup( msg, PF_GET_KEY );
        return INT_MIN;
    }

    return inv_s.execute_pick( title );
}

int game::inv_for_all( const std::string &title, const std::string &none_message )
{
    const std::string msg = ( none_message.empty() ) ? _( "Your inventory is empty." ) : none_message;
    return inv_for_filter( title, allow_all_items, msg );
}

int game::inv_for_activatables( const player &p, const std::string &title )
{
    return inv_for_filter( title, [ &p ]( const item &it ) {
        return p.rate_action_use( it ) != HINT_CANT;
    }, _( "You don't have any items you can use." ) );
}

int game::inv_for_flag( const std::string &flag, const std::string &title )
{
    return inv_for_filter( title, [ &flag ]( const item &it ) {
        return it.has_flag( flag );
    } );
}

int game::inv_for_id( const itype_id &id, const std::string &title )
{
    return inv_for_filter( title, [ &id ]( const item &it ) {
        return it.type->id == id;
    }, string_format( _( "You don't have a %s." ), item::nname( id ).c_str() ) );
}

int game::inv_for_tools_powered_by( const ammotype &battery_id, const std::string &title )
{
    return inv_for_filter( title, [ &battery_id ]( const item & it ) {
        return it.is_tool() && it.ammo_type() == battery_id;
    }, string_format( _( "You don't have %s-powered tools." ), ammo_name( battery_id ).c_str() ) );
}

int game::inv_for_equipped( const std::string &title )
{
    return inv_for_filter( title, [ this ]( const item &it ) {
        return u.is_worn( it );
    }, _( "You don't wear anything." ) );
}

int game::inv_for_unequipped( const std::string &title )
{
    return inv_for_filter( title, [ this ]( const item &it ) {
        return it.is_armor() && !u.is_worn( it );
    }, _( "You don't have any items to wear." ) );
}

item_location game::inv_map_splice( item_filter filter, const std::string &title, int radius,
                                    const std::string &none_message )
{
    return inv_map_splice( filter, filter, filter, title, radius, none_message );
}

item_location game::inv_map_splice(
    item_filter inv_filter, item_filter ground_filter, item_filter vehicle_filter,
    const std::string &title, int radius, const std::string &none_message )
{
    u.inv.restack( &u );
    u.inv.sort();

    inventory_selector inv_s( u, inv_filter );

    std::list<item_category> categories;
    int rank = -1000;

    // items are stacked per tile considering vehicle and map tiles separately
    // in the below loops identical items on the same tile are grouped into lists
    // each element of stacks represents one tile and is a vector of such lists
    std::vector<std::vector<std::list<item>>> stacks;

    // an indexed_invslice is created for each map or vehicle tile
    // each list of items created above for the tile will be added to it
    std::vector<indexed_invslice> slices;

    // inv_s.first_item will later contain the chosen item as a pointer to first item
    // of one of the above lists so use this as the key when storing the item location
    std::unordered_map<item *, item_location> opts;

    // the closest 10 items also have their location added to the invlets vector
    const char min_invlet = '0';
    const char max_invlet = '9';
    char cur_invlet = min_invlet;

    for( const auto &pos : closest_tripoints_first( radius, g->u.pos() ) ) {
        // second get all matching items on the map within radius
        if( m.accessible_items( g->u.pos(), pos, radius ) ) {
            auto items = m.i_at( pos );

            // create a new slice and stack for the current map tile
            stacks.emplace_back();
            slices.emplace_back();

            // reserve sufficient capacity to ensure reallocation is not required
            auto &current_stack = stacks.back();
            current_stack.reserve( items.size() );

            for( item &it : items ) {
                if( ground_filter( it ) ) {
                    auto match = std::find_if( current_stack.begin(),
                    current_stack.end(), [&]( const std::list<item> &e ) {
                        return it.stacks_with( e.back() );
                    } );
                    if( match != current_stack.end() ) {
                        match->push_back( it );
                    } else {
                        // item doesn't stack with any previous so start new list and append to current indexed_invslice
                        current_stack.emplace_back( 1, it );
                        slices.back().emplace_back( &current_stack.back(), INT_MIN );
                        opts.emplace( &current_stack.back().front(), item_location( pos, &it ) );

                        current_stack.back().front().invlet = ( cur_invlet <= max_invlet ) ? cur_invlet++ : 0;
                    }
                }
            }
            std::string name = trim( std::string( _( "GROUND" ) ) + " " + direction_suffix( g->u.pos(), pos ) );
            categories.emplace_back( name, name, rank-- );
            inv_s.add_items( slices.back(), add_to::END, &categories.back() );
        }

        // finally get all matching items in vehicle cargo spaces
        int part = -1;
        vehicle *veh = m.veh_at( pos, part );
        if( veh && part >= 0 ) {
            part = veh->part_with_feature( part, "CARGO" );
            if( part != -1 ) {
                auto items = veh->get_items( part );

                // create a new slice and stack for the current vehicle part
                stacks.emplace_back();
                slices.emplace_back();

                // reserve sufficient capacity to ensure reallocation is not required
                auto &current_stack = stacks.back();
                current_stack.reserve( items.size() );

                for( item &it : items ) {
                    if( vehicle_filter( it ) ) {
                        auto match = std::find_if( current_stack.begin(),
                        current_stack.end(), [&]( const std::list<item> &e ) {
                            return it.stacks_with( e.back() );
                        } );
                        if( match != current_stack.end() ) {
                            match->push_back( it );
                        } else {
                            // item doesn't stack with any previous so start new list and append to current indexed_invslice
                            current_stack.emplace_back( 1, it );
                            slices.back().emplace_back( &current_stack.back(), INT_MIN );
                            opts.emplace( &current_stack.back().front(), item_location( vehicle_cursor( *veh, part ), &it ) );

                            current_stack.back().front().invlet = ( cur_invlet <= max_invlet ) ? cur_invlet++ : 0;
                        }
                    }
                }
                std::string name = trim( std::string( _( "VEHICLE" ) )  + " " + direction_suffix( g->u.pos(), pos ) );
                categories.emplace_back( name, name, rank-- );
                inv_s.add_items( slices.back(), add_to::END, &categories.back() );
            }
        }
    }

    if( inv_s.empty() ) {
        const std::string msg = ( none_message.empty() ) ? _( "You don't have the necessary item at hand." ) : none_message;
        popup( msg, PF_GET_KEY );
        return item_location();
    }
    return inv_s.execute_pick_map( title, opts );
}

item *game::inv_map_for_liquid(const item &liquid, const std::string &title, int radius)
{
    auto sealable_filter = [&]( const item &candidate ) {
        return candidate.get_remaining_capacity_for_liquid( liquid, false ) > 0;
    };

    auto bucket_filter = [&]( const item &candidate ) {
        return candidate.get_remaining_capacity_for_liquid( liquid, true ) > 0;
    };

    // Buckets can only be filled when on the ground
    return inv_map_splice( sealable_filter, bucket_filter, sealable_filter, title, radius,
                           string_format( _( "You don't have a suitable container for carrying %s." ),
                           liquid.type_name( 1 ).c_str() ) ).get_item();
}

int inventory::num_items_at_position( int const position )
{
    if( position < -1 ) {
        const item& armor = g->u.i_at( position );
        return armor.count_by_charges() ? armor.charges : 1;
    } else if( position == -1 ) {
        return g->u.weapon.count_by_charges() ? g->u.weapon.charges : 1;
    } else {
        const std::list<item> &stack = g->u.inv.const_stack(position);
        if( stack.size() == 1 ) {
            return stack.front().count_by_charges() ?
                stack.front().charges : 1;
        } else {
            return stack.size();
        }
    }
}

std::list<std::pair<int, int>> game::multidrop()
{
    u.inv.restack( &u );
    u.inv.sort();

    inventory_selector inv_s( u, [ this ]( const item &it ) -> bool {
        return u.can_unwield( it, false );
    } );
    if( inv_s.empty() ) {
        popup( std::string( _( "You have nothing to drop." ) ), PF_GET_KEY );
        return std::list<std::pair<int, int> >();
    }
    return inv_s.execute_multidrop( _( "Multidrop:" ) );
}

void game::compare()
{
    tripoint dir;
    int &dirx = dir.x;
    int &diry = dir.y;

    if( choose_direction(_("Compare where?"), dirx, diry ) ) {
        refresh_all();
        compare( tripoint( dirx, diry, 0 ) );
    }
    refresh_all();
}

void game::compare( const tripoint &offset )
{
    const tripoint examp = u.pos() + offset;

    std::vector<std::list<item>> grounditems;
    indexed_invslice grounditems_slice;

    if( !m.has_flag( "SEALED", u.pos() ) ) {
        auto here = m.i_at( examp );
        //Filter out items with the same name (keep only one of them)
        std::set<std::string> dups;
        for (size_t i = 0; i < here.size(); i++) {
            if (dups.count(here[i].tname()) == 0) {
                grounditems.push_back(std::list<item>(1, here[i]));

                //Only the first 10 items get a invlet
                if ( grounditems.size() <= 10 ) {
                    // invlet: '0' ... '9'
                    grounditems.back().front().invlet = '0' + grounditems.size() - 1;
                }

                dups.insert(here[i].tname());
            }
        }
        for (size_t a = 0; a < grounditems.size(); a++) {
            // avoid INT_MIN, as it can be confused with "no item at all"
            grounditems_slice.push_back(indexed_invslice::value_type(&grounditems[a], INT_MIN + a + 1));
        }
    }
    static const item_category category_on_ground(
        "GROUND:",
        _("GROUND:"),
        -1000
    );

    u.inv.restack(&u);
    u.inv.sort();

    inventory_selector inv_s( u );

    inv_s.add_items( grounditems_slice, add_to::BEGINNING, &category_on_ground );
    if( inv_s.empty() ) {
        popup( std::string( _( "There are no items to compare." ) ), PF_GET_KEY );
        return;
    }
    inv_s.execute_compare( _( "Compare:" ) );
}
