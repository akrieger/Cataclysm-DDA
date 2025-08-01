#pragma once
#ifndef CATA_SRC_OVERMAP_H
#define CATA_SRC_OVERMAP_H

#include <stdint.h>
#include <algorithm>
#include <array>
#include <bitset>
#include <climits>
#include <cstdlib>
#include <functional>
#include <iosfwd>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "basecamp.h"
#include "cata_variant.h"
#include "city.h"
#include "colony.h"
#include "color.h"
#include "coordinates.h"
#include "cube_direction.h"
#include "enums.h"
#include "hash_utils.h"
#include "map_scale_constants.h"
#include "mapgendata.h"
#include "mdarray.h"
#include "memory_fast.h"
#include "mongroup.h"
#include "monster.h"
#include "omdata.h"
#include "overmap_types.h" // IWYU pragma: keep
#include "point.h"
#include "rng.h"
#include "simple_pathfinding.h"
#include "type_id.h"

class JsonArray;
class JsonObject;
class JsonOut;
class JsonValue;
class cata_path;
class character_id;
class npc;
class overmap_connection;
struct regional_settings;
template <typename T> struct enum_traits;

struct om_note {
    std::string text;
    point_om_omt p;
    bool dangerous = false;
    int danger_radius = 0;
};

struct om_map_extra {
    map_extra_id id;
    point_om_omt p;
};

struct om_vehicle {
    tripoint_om_omt p; // overmap coordinates of tracked vehicle
    std::string name;
};

enum class radio_type : int {
    MESSAGE_BROADCAST,
    WEATHER_RADIO
};

extern std::map<radio_type, std::string> radio_type_names;

constexpr int RADIO_MIN_STRENGTH = 120;
constexpr int RADIO_MAX_STRENGTH = 360;
/*
* Indentation from edge of overmap where neighbouring rivers and nodes aren't checked
* to avoid corners, where starts/ends would be arbitrary
* TODO: could be smaller?
*/
constexpr int RIVER_BORDER = 10;
constexpr int RIVER_Z = 0;
constexpr int HIGHWAY_MAX_CONNECTIONS = 4;

struct radio_tower {
    // local (to the containing overmap) submap coordinates
    point_om_sm pos;
    int strength;
    radio_type type;
    std::string message;
    int frequency;
    explicit radio_tower( const point_om_sm &p, int S = -1, const std::string &M = "",
                          radio_type T = radio_type::MESSAGE_BROADCAST ) :
        pos( p ), strength( S ), type( T ), message( M ) {
        frequency = rng( 0, INT_MAX );
    }
};

enum class om_vision_level : int8_t {
    unseen = 0,
    // Vague details from a quick glance
    // Broad geographical features - forest,field,buildings,water
    vague,
    // A scan from a distance
    // Track roads, distinguish obvious features (farms from fields)
    outlines,
    // Detailed scan from a distance
    // General building types, some hard to spot features become clear
    details,
    // Full knowledge of the tile
    full,
    last
};

template<>
struct enum_traits<om_vision_level> {
    static constexpr om_vision_level last = om_vision_level::last;
};

struct map_layer {
    cata::mdarray<oter_id, point_om_omt> terrain;
    cata::mdarray<om_vision_level, point_om_omt> visible;
    cata::mdarray<bool, point_om_omt> explored;
    std::vector<om_note> notes;
    std::vector<om_map_extra> extras;
};

struct om_special_sectors {
    std::vector<point_om_omt> sectors;
    int sector_width;
};

// Wrapper around an overmap special to track progress of placing specials.
struct overmap_special_placement {
    int instances_placed;
    const overmap_special *special_details;
};

// Wrapper around a river node to contain river data.
// Could be used to determine entry/exit points for boats across overmaps.
// Could also contain name.
struct overmap_river_node {
    const point_om_omt river_start;
    const point_om_omt river_end;
    point_om_omt control_p1 = point_om_omt::invalid; // control points for the Bezier curve
    point_om_omt control_p2 = point_om_omt::invalid;
    const size_t size = 0; // total omt of this river
    bool is_branch = false; //was this river placed by place_rivers(), or place_river()?
};

// River data gathered from an existing adjacent overmap
struct overmap_river_border {
    std::vector<point_om_omt> border_river_omt; // OMT river points from the adjacent overmap's border
    std::vector<point_om_omt>
    border_river_nodes_omt; // OMT river node points from the adjacent overmap's border
    std::vector<const overmap_river_node *>
    border_river_nodes; //river nodes from the adjacent overmap's border
};

// Contains positions of highway entries, exits, and intersections for an overmap
struct overmap_highway_node {
    std::array<point_om_omt, HIGHWAY_MAX_CONNECTIONS> highway_ends;
};

// A batch of overmap specials to place.
class overmap_special_batch
{
    public:
        explicit overmap_special_batch( const point_abs_om &origin ) : origin_overmap( origin ) {}
        overmap_special_batch( const point_abs_om &origin,
                               const std::vector<const overmap_special *> &specials ) :
            origin_overmap( origin ) {
            std::transform( specials.begin(), specials.end(),
            std::back_inserter( placements ), []( const overmap_special * elem ) {
                return overmap_special_placement{ 0, elem };
            } );
        }

        // Wrapper methods that make overmap_special_batch act like
        // the underlying vector of overmap placements.
        std::vector<overmap_special_placement>::iterator begin() {
            return placements.begin();
        }
        std::vector<overmap_special_placement>::const_iterator begin() const {
            return placements.begin();
        }
        std::vector<overmap_special_placement>::iterator end() {
            return placements.end();
        }
        std::vector<overmap_special_placement>::const_iterator end() const {
            return placements.end();
        }
        std::vector<overmap_special_placement>::iterator erase(
            std::vector<overmap_special_placement>::iterator pos ) {
            return placements.erase( pos );
        }
        bool empty() {
            return placements.empty();
        }

        point_abs_om get_origin() const {
            return origin_overmap;
        }

    private:
        std::vector<overmap_special_placement> placements;
        point_abs_om origin_overmap;
};

/**
* Highways use a grid of overmap points to determine where intersections are generated.
* These grid points have an offset to make highways look more natural.
*/
struct interhighway_node {
    interhighway_node() = default;
    explicit interhighway_node( point_abs_om grid_pos ) : grid_pos( grid_pos ) {};
    //offset position; effectively where the intersection special is placed
    point_abs_om offset_pos = point_abs_om::invalid;
    //grid point; used to bound any given overmap
    point_abs_om grid_pos = point_abs_om::invalid;
    //existing N/E/S/W adjacent intersections
    std::array<point_abs_om, HIGHWAY_MAX_CONNECTIONS> adjacent_intersections =
    { point_abs_om::invalid, point_abs_om::invalid, point_abs_om::invalid, point_abs_om::invalid };
    //set offset_pos for this point
    void generate_offset( int intersection_max_radius );
    void serialize( JsonOut &out ) const;
    void deserialize( const JsonObject &obj );
};

/*
* Contains pre - generated data for where and in what direction highway
* segments/specials get placed on the scale of one overmap.
* also contains whether the piece of highway is a segment or a special,
* and (if applicable) if the segment is a ramp or not
*/
struct intrahighway_node {
    pf::directed_node<tripoint_om_omt> path_node =
        pf::directed_node<tripoint_om_omt>( tripoint_om_omt::invalid, om_direction::type::invalid );
    overmap_special_id placed_special = overmap_special_id::NULL_ID();
    bool is_segment = true;
    bool is_ramp = false;
    //whether to flip ramp direction
    bool ramp_down = false;
    explicit intrahighway_node( tripoint_om_omt pos, om_direction::type dir,
                                overmap_special_id placed_spec, bool is_seg = true ) {
        path_node = pf::directed_node<tripoint_om_omt>( pos, dir );
        placed_special = placed_spec;
        is_segment = is_seg;
    }

    //highways segments are locked to N/E in placement
    om_direction::type get_effective_dir() const {
        return is_segment ?
               om_direction::type( static_cast<int>( path_node.dir ) % 2 ) :
               om_direction::type( static_cast<int>( path_node.dir ) );
    }
};

template<typename Tripoint>
struct pos_dir {
    Tripoint p;
    cube_direction dir;

    pos_dir opposite() const;

    void serialize( JsonOut &jsout ) const;
    void deserialize( const JsonArray &ja );

    bool operator==( const pos_dir &r ) const;
    bool operator<( const pos_dir &r ) const;
};

extern template struct pos_dir<tripoint_om_omt>;
extern template struct pos_dir<tripoint_rel_omt>;

using om_pos_dir = pos_dir<tripoint_om_omt>;
using rel_pos_dir = pos_dir<tripoint_rel_omt>;
using Highway_path = std::vector<intrahighway_node>;

template<typename Tripoint>
// NOLINTNEXTLINE(cert-dcl58-cpp)
struct std::hash<pos_dir<Tripoint>> {
    std::size_t operator()( const pos_dir<Tripoint> &p ) const {
        cata::tuple_hash h;
        return h( std::make_tuple( p.p, p.dir ) );
    }
};

class overmap
{
    public:
        overmap( const overmap & ) = default;
        overmap( overmap && ) = default;
        explicit overmap( const point_abs_om &p );
        ~overmap();

        overmap &operator=( const overmap & ) = default;

        /**
         * Create content in the overmap.
         **/
        void populate( overmap_special_batch &enabled_specials );
        void populate();

        const point_abs_om &pos() const {
            return loc;
        }
        int get_urbanity() const {
            return urbanity;
        }
        //will this OMT contain a lake before or after it is generated?
        static bool omt_lake_noise_threshold( const point_abs_omt &origin, const point_om_omt &offset,
                                              double noise_threshold );
        //does the overmap have at least one lake OMT?
        //TODO: extend pre-determined lake generation so we know instead of guess
        static bool guess_has_lake( const point_abs_om &p, double noise_threshold, int tile_count );

        void save() const;

        /**
         * @return The (local) overmap terrain coordinates of a randomly
         * chosen place on the overmap with the specific overmap terrain.
         * Returns @ref tripoint_om_omt::invalid if no suitable place has been found.
         */
        tripoint_om_omt find_random_omt( const std::pair<std::string, ot_match_type> &target,
                                         std::optional<city> target_city = std::nullopt ) const;
        tripoint_om_omt find_random_omt( const std::string &omt_base_type,
                                         ot_match_type match_type = ot_match_type::type,
                                         std::optional<city> target_city = std::nullopt ) const {
            return find_random_omt( std::make_pair( omt_base_type, match_type ), std::move( target_city ) );
        }
        /**
         * Return a vector containing the absolute coordinates of
         * every matching terrain on the current z level of the current overmap.
         * @returns A vector of terrain coordinates (absolute overmap terrain
         * coordinates), or empty vector if no matching terrain is found.
         */
        std::vector<point_abs_omt> find_terrain( std::string_view term, int zlevel ) const;

        void ter_set( const tripoint_om_omt &p, const oter_id &id );
        // ter has bounds checking, and returns ot_null when out of bounds.
        const oter_id &ter( const tripoint_om_omt &p ) const;
        // ter_unsafe is UB when out of bounds.
        const oter_id &ter_unsafe( const tripoint_om_omt &p ) const;
        std::optional<mapgen_arguments> *mapgen_args( const tripoint_om_omt & );
        std::string *join_used_at( const om_pos_dir & );
        std::vector<oter_id> predecessors( const tripoint_om_omt & );
        void set_seen( const tripoint_om_omt &p, om_vision_level val, bool force = false );
        om_vision_level seen( const tripoint_om_omt &p ) const;
        bool seen_more_than( const tripoint_om_omt &p, om_vision_level test ) const;
        bool &explored( const tripoint_om_omt &p );
        bool is_explored( const tripoint_om_omt &p ) const;

        bool has_note( const tripoint_om_omt &p ) const;
        bool is_marked_dangerous( const tripoint_om_omt &p ) const;
        const std::string &note( const tripoint_om_omt &p ) const;
        void add_note( const tripoint_om_omt &p, std::string message );
        void delete_note( const tripoint_om_omt &p );
        void mark_note_dangerous( const tripoint_om_omt &p, int radius, bool is_dangerous );
        int note_danger_radius( const tripoint_om_omt &p ) const;

        bool has_extra( const tripoint_om_omt &p ) const;
        const map_extra_id &extra( const tripoint_om_omt &p ) const;
        void add_extra( const tripoint_om_omt &p, const map_extra_id &id );
        void add_extra_note( const tripoint_om_omt &p );
        void delete_extra( const tripoint_om_omt &p );

        /**
         * Getter for overmap scents.
         * @returns a reference to a scent_trace from the requested location.
         */
        const scent_trace &scent_at( const tripoint_abs_omt &loc ) const;
        /**
         * Setter for overmap scents, stores the provided scent at the provided location.
         */
        void set_scent( const tripoint_abs_omt &loc, const scent_trace &new_scent );

        /**
         * @returns Whether @param p is within desired bounds of the overmap
         * @param clearance Minimal distance from the edges of the overmap
         */
        static bool inbounds( const tripoint_om_omt &p, int clearance = 0 );
        static bool inbounds( const point_om_omt &p, int clearance = 0 ) {
            return inbounds( tripoint_om_omt( p, 0 ), clearance );
        }
        /**
         * Return a vector containing the absolute coordinates of
         * every matching note on the current z level of the current overmap.
         * @returns A vector of note coordinates (absolute overmap terrain
         * coordinates), or empty vector if no matching notes are found.
         */
        std::vector<point_abs_omt> find_notes( int z, const std::string &text );
        /**
         * Return a vector containing the absolute coordinates of
         * every matching map extra on the current z level of the current overmap.
         * @returns A vector of map extra coordinates (absolute overmap terrain
         * coordinates), or empty vector if no matching map extras are found.
         */
        std::vector<point_abs_omt> find_extras( int z, const std::string &text );

        /**
         * Returns whether or not the location has been generated (e.g. mapgen has run).
         * @param loc Location to check.
         * @returns True if param @loc has been generated.
         */
        bool is_omt_generated( const tripoint_om_omt &loc ) const;

        /* Returns true if position is an entry/exit position of a river node. */
        bool is_river_node( const point_om_omt &p ) const;

        /* Returns the overmap river node if the position is an entry/exit node of river. */
        const overmap_river_node *get_river_node_at( const point_om_omt &p ) const;

        /** Returns the (0, 0) corner of the overmap in the global coordinates. */
        point_abs_omt global_base_point() const;

        // TODO: Should depend on coordinates
        const regional_settings &get_settings() const {
            return *settings;
        }

        void clear_mon_groups();
        void clear_overmap_special_placements();
        void clear_cities();
        void clear_connections_out();
        void place_special_forced( const overmap_special_id &special_id, const tripoint_om_omt &p,
                                   om_direction::type dir );
        // Whether the tripoint's point is true in city_tiles
        bool is_in_city( const tripoint_om_omt &p ) const;
        // Returns the distance to the nearest city_tile within max_dist_to_check or std::nullopt if there isn't one
        std::optional<int> distance_to_city( const tripoint_om_omt &p,
                                             int max_dist_to_check = OMAPX ) const;
        // Returns the distance to the nearest city boundary within max_dist_to_check or std::nullopt if there isn't one
        // Returns 0 if within the bounds of any city.
        std::optional<int> approx_distance_to_city( const tripoint_om_omt &p,
                int max_dist_to_check = OMAPX ) const;
    private:
        // Any point that is part of or surrounded by a city
        std::unordered_set<point_om_omt> city_tiles;
        // Fill in any gaps in city_tiles that don't connect to the map edge
        void flood_fill_city_tiles();
        std::multimap<tripoint_om_sm, mongroup> zg; // NOLINT(cata-serialize)
    public:
        /** Unit test enablers to check if a given mongroup is present. */
        bool mongroup_check( const mongroup &candidate ) const;
        bool monster_check( const std::pair<tripoint_om_sm, monster> &candidate ) const;

        // TODO: make private
        std::vector<radio_tower> radios;
        std::map<int, om_vehicle> vehicles;
        std::vector<basecamp> camps;
        std::vector<city> cities;
        std::vector<overmap_river_node> rivers;
        std::map<string_id<overmap_connection>, std::vector<tripoint_om_omt>> connections_out;
        std::optional<basecamp *> find_camp( const point_abs_omt &p );
        /// Adds the npc to the contained list of npcs ( @ref npcs ).
        void insert_npc( const shared_ptr_fast<npc> &who );
        /// Removes the npc and returns it ( or returns nullptr if not found ).
        shared_ptr_fast<npc> erase_npc( const character_id &id );
        shared_ptr_fast<npc> find_npc( const character_id &id ) const;
        shared_ptr_fast<npc> find_npc_by_unique_id( const std::string &id ) const;
        const std::vector<shared_ptr_fast<npc>> &get_npcs() const {
            return npcs;
        }
        std::vector<shared_ptr_fast<npc>> get_npcs( const std::function<bool( const npc & )>
                                       &predicate )
                                       const;
        point_om_omt get_fallback_road_connection_point() const;
    private:
        friend class overmapbuffer;

        std::vector<shared_ptr_fast<npc>> npcs;

        // A fake boolean that's returned for out-of-bounds calls to
        // overmap::seen and overmap::explored
        bool nullbool = false; // NOLINT(cata-serialize)
        point_abs_om loc; // NOLINT(cata-serialize)
        // Random point used for special connections if there's no cities on the overmap, joins to all roads_out
        std::optional<point_om_omt> fallback_road_connection_point; // NOLINT(cata-serialize)

        // Whether this overmap has a highway connection point at this direction (N/E/S/W)
        std::array<tripoint_om_omt, 4> highway_connections;

        std::array<map_layer, OVERMAP_LAYERS> layer;
        std::unordered_map<tripoint_abs_omt, scent_trace> scents;

        // Records the locations where a given overmap special was placed, which
        // can be used after placement to lookup whether a given location was created
        // as part of a special.
        std::unordered_map<tripoint_om_omt, overmap_special_id> overmap_special_placements;
        // Records location where mongroups are not allowed to spawn during worldgen.
        // Reconstructed on load, so need not be serialized.
        std::unordered_set<tripoint_om_omt> safe_at_worldgen; // NOLINT(cata-serialize)

        // For oter_ts with the requires_predecessor flag, we need to store the
        // predecessor terrains so they can be used for mapgen later
        std::unordered_map<tripoint_om_omt, std::vector<oter_id>> predecessors_;

        // Records mapgen parameters required at the overmap special level
        // These are lazily evaluated; empty optional means that they have yet
        // to be evaluated.
        cata::colony<std::optional<mapgen_arguments>> mapgen_arg_storage;
        std::unordered_map<tripoint_om_omt, std::optional<mapgen_arguments> *> mapgen_args_index;
        // Records mapgen parameters required at the omt level, fixed at the same values vertically
        std::unordered_map<point_abs_omt, mapgen_arguments> omt_stack_arguments_map;

        // Records the joins that were chosen during placement of a mutable
        // special, so that it can be queried later by mapgen
        std::unordered_map<om_pos_dir, std::string> joins_used;

        const regional_settings *settings;

        oter_id get_default_terrain( int z ) const;

        // Initialize
        void init_layers();
        // open existing overmap, or generate a new one
        void open( overmap_special_batch &enabled_specials );
    public:
        // Get all values from omt_stack_arguments_map at the given point or nullopt if not set yet
        std::optional<mapgen_arguments> get_existing_omt_stack_arguments(
            const point_abs_omt &p ) const;
        // Get value from omt_stack_arguments_map or nullopt if not set yet
        std::optional<cata_variant> get_existing_omt_stack_argument( const point_abs_omt &p,
                const std::string &param_name ) const;
        // Set a value in omt_stack_arguments_map
        void add_omt_stack_argument( const point_abs_omt &p, const std::string &param_name,
                                     const cata_variant &value );
        /**
         * When monsters despawn during map-shifting they will be added here.
         * map::spawn_monsters will load them and place them into the reality bubble
         * (adding it to the creature tracker and putting it onto the map).
         * This stores each submap worth of monsters in a different bucket of the multimap.
         */
        std::unordered_multimap<tripoint_om_sm, monster> monster_map;

        // parse data in an opened overmap file
        void unserialize( const cata_path &file_name, std::istream &fin );
        void unserialize( std::istream &fin );
        void unserialize( const JsonObject &jsobj );
        // parse data in an opened omap file
        void unserialize_omap( const JsonValue &jsin, const cata_path &json_path );
        // Parse per-player overmap view data.
        void unserialize_view( const cata_path &file_name, std::istream &fin );
        void unserialize_view( const JsonObject &jsobj );
        // Save data in an opened overmap file
        void serialize( std::ostream &fout ) const;
        // Save per-player overmap view data.
        void serialize_view( std::ostream &fout ) const;
    private:
        void generate( const std::vector<const overmap *> &neighbor_overmaps,
                       overmap_special_batch &enabled_specials );
        bool generate_sub( int z );
        bool generate_over( int z );
        // Check and put bridgeheads
        void generate_bridgeheads( const std::vector<point_om_omt> &bridge_points,
                                   oter_type_str_id bridge_type,
                                   const std::string &bridgehead_ground,
                                   const std::string &bridgehead_ramp );

        const city &get_nearest_city( const tripoint_om_omt &p ) const;
        const city &get_invalid_city() const;

        void signal_hordes( const tripoint_rel_sm &p, int sig_power );
        void process_mongroups();
        void move_hordes();

        //nemesis movement for "hunted" trait
        void signal_nemesis( const tripoint_abs_sm & );
        void move_nemesis();
        void place_nemesis( const tripoint_abs_omt & );
        bool remove_nemesis(); // returns true if nemesis found and removed

        // code deduplication - calc ocean gradient
        float calculate_ocean_gradient( const point_om_omt &p, point_abs_om this_omt );
        /*
        * places an individual river; see place_rivers()
        * @param temp_node precalculated points for river; should NOT be stored in overmap
        */
        void place_river( const std::vector<const overmap *> &neighbor_overmaps,
                          const overmap_river_node &initial_points, int river_scale = 1.0, bool major_river = false );
        void place_forests();
        void place_lakes( const std::vector<const overmap *> &neighbor_overmaps );
        void place_oceans( const std::vector<const overmap *> &neighbor_overmaps );
        void place_rivers( const std::vector<const overmap *> &neighbor_overmaps );

        void place_swamps();
        void place_forest_trails();
        void place_forest_trailheads();

        void place_roads( const std::vector<const overmap *> &neighbor_overmaps );
        /**
        * Draws paths for highways, placing reserved highway terrain and non-highway-segment specials
        * @return all highway paths from edges of overmaps to intersections or other overmap edges (up to 4 paths)
        */
        std::vector<Highway_path> place_highways( const std::vector<const overmap *>
                &neighbor_overmaps );
        /**
        * sub-function for place_highway_reserved_path
        * draws a single line segment of highway, placing slants
        * draw_direction is from sp1 to sp2
        */
        Highway_path place_highway_line(
            const tripoint_om_omt &sp1, const tripoint_om_omt &sp2,
            const om_direction::type &draw_direction, int base_z );
        /**
        * sub-function for place_highway_reserved_path
        * draws multiple line segments of highway, connected by bends
        * draw_direction is from sp1 to sp2
        */
        void place_highway_lines_with_bends( Highway_path &highway_path,
                                             const std::vector<std::pair<tripoint_om_omt, om_direction::type>> &bend_points,
                                             const tripoint_om_omt &start_point, const tripoint_om_omt &end_point,
                                             om_direction::type direction, int base_z );
        /**
        * sub-function for place_highways
        * lays out reserved OMTs for highway segments, and places non-segment specials
        */
        Highway_path place_highway_reserved_path( const tripoint_om_omt &p1,
                const tripoint_om_omt &p2,
                int dir1, int dir2, int base_z );
        /*
        * tries to find a point to place an intersection special where it isn't on water
        * @return point in radius of center without water; invalid if none found
        */
        tripoint_om_omt find_highway_intersection_point( const overmap_special_id &special,
                const tripoint_om_omt &center, const om_direction::type &dir, int border ) const;
        /* @return whether the intersection is far enough away (using border) to the edge of the map */
        bool check_intersection_inbounds( const overmap_special_id &special,
                                          const tripoint_om_omt &center, int border ) const;
        //segments adjacent to specials must have the special's z-value for correct ramp handling
        void highway_handle_special_z( Highway_path &highway_path, int base_z );
        // determine which overmaps have adjacent oceans (if applicable)
        // @return { abort highway generation, adjacent ocean overmaps }
        std::pair<bool, std::bitset<HIGHWAY_MAX_CONNECTIONS>> highway_handle_oceans();
        // determine end points for a highway in this overmap, given existing neighboring overmaps/oceans
        // @return success, selected end points in corresponding parameter
        bool highway_select_end_points( const std::vector<const overmap *> &neighbor_overmaps,
                                        std::array<tripoint_om_omt, HIGHWAY_MAX_CONNECTIONS> &end_points,
                                        std::bitset<HIGHWAY_MAX_CONNECTIONS> &neighbor_connections,
                                        const std::bitset<HIGHWAY_MAX_CONNECTIONS> &ocean_neighbors, int base_z );
        //given a path of highway nodes, remove small gaps of land in raised segments
        void highway_handle_ramps( Highway_path &path, int base_z );
        //places highway special, replacing fallback supports with mutable supports that extend downwards
        void place_highway_supported_special( const overmap_special_id &special,
                                              const tripoint_om_omt &placement,
                                              const om_direction::type &dir );
        // Replace reserved OMTs with necessary specials given what has drawn over them since place_highways()
        void finalize_highways( std::vector<Highway_path> &paths );
        bool highway_intersection_exists( const point_abs_om &intersection_om ) const;
        /**
        * is this an overmap containing a highway?
        * @return adjacent N/E/S/W overmaps containing highways, if applicable
        */
        std::optional<std::bitset<4>> is_highway_overmap() const;
        /*
        * gets all points drawn for exactly one highway segment special, given a highway path node
        * @return { set of points, offset direction from initial node point }
        */
        std::pair<std::vector<tripoint_om_omt>, om_direction::type>
        get_highway_segment_points( const pf::directed_node<tripoint_om_omt> &node ) const;

        void place_railroads( const std::vector<const overmap *> &neighbor_overmaps );

        void populate_connections_out_from_neighbors( const std::vector<const overmap *>
                &neighbor_overmaps );

        void log_unique_special( const overmap_special_id &id );
        bool contains_unique_special( const overmap_special_id &id ) const;

        /*
        * checks adjacent overmap in direction for river terrain bordering this overmap
        * will not generate the adjacent overmap!
        * @param border -- don't check this much from the corners
        * @param is_river_node -- additionally check if the river is a river node
        * @return { list of points on *calling* overmap that border rivers,
        list of points on *neighbor* overmap that contain river nodes }
        */
        overmap_river_border setup_adjacent_river( const point_rel_om &adjacent_om, int border );

        // City Building
        overmap_special_id pick_random_building_to_place( int town_dist, int town_size,
                const std::unordered_set<overmap_special_id> &placed_unique_buildings ) const;

        // urbanity and forestosity are biome stats that can be used to trigger changes in biome.
        // NOLINTNEXTLINE(cata-serialize)
        int urbanity = 0;
        // forest_size_adjust is basically the same as forestosity, but forestosity is
        // scaled to be comparable to urbanity and other biome stats.
        // NOLINTNEXTLINE(cata-serialize)
        float forest_size_adjust = 0.0f;
        // NOLINTNEXTLINE(cata-serialize)
        float forestosity = 0.0f;
        void calculate_urbanity();
        void calculate_forestosity();

        void place_cities();
        void place_building( const tripoint_om_omt &p, om_direction::type dir, const city &town,
                             std::unordered_set<overmap_special_id> &placed_unique_buildings );

        void build_city_street( const overmap_connection &connection, const point_om_omt &p, int cs,
                                om_direction::type dir, const city &town,
                                std::unordered_set<overmap_special_id> &placed_unique_buildings, int block_width = 2 );
        bool build_lab( const tripoint_om_omt &p, int s, std::vector<point_om_omt> *lab_train_points,
                        const std::string &prefix, int train_odds );
        void place_ravines();

        // Connection laying
        pf::directed_path<point_om_omt> lay_out_connection(
            const overmap_connection &connection, const point_om_omt &source,
            const point_om_omt &dest, int z, bool must_be_unexplored ) const;
        pf::directed_path<point_om_omt> lay_out_street(
            const overmap_connection &connection, const point_om_omt &source,
            om_direction::type dir, size_t len );
    public:
        void build_connection(
            const overmap_connection &connection, const pf::directed_path<point_om_omt> &path, int z,
            cube_direction initial_dir = cube_direction::last );
        void build_connection( const point_om_omt &source, const point_om_omt &dest, int z,
                               const overmap_connection &connection, bool must_be_unexplored,
                               cube_direction initial_dir = cube_direction::last );
        void connect_closest_points( const std::vector<point_om_omt> &points, int z,
                                     const overmap_connection &connection );
        // Polishing
        bool check_ot( const std::string &otype, ot_match_type match_type,
                       const tripoint_om_omt &p ) const;
        bool check_overmap_special_type( const overmap_special_id &id,
                                         const tripoint_om_omt &location ) const;
        std::optional<overmap_special_id> overmap_special_at( const tripoint_om_omt &p ) const;

        //gets border OMT points of this overmap in cardinal direction
        std::vector<tripoint_om_omt> get_border( const point_rel_om &direction, int z,
                int distance_corner );
        //gets border OMT points of the neighboring overmap in cardinal direction
        std::vector<tripoint_om_omt> get_neighbor_border( const point_rel_om &direction, int z,
                int distance_corner );
        void polish_river( const std::vector<const overmap *> &neighbor_overmaps );
        void build_river_shores( const std::vector<const overmap *> &neighbor_overmaps,
                                 const tripoint_om_omt &p );
        void river_meander( const point_om_omt &river_end, tripoint_om_omt &current_point,
                            int river_scale );

        om_direction::type random_special_rotation( const overmap_special &special,
                const tripoint_om_omt &p, bool must_be_unexplored ) const;

        bool can_place_special( const overmap_special &special, const tripoint_om_omt &p,
                                om_direction::type dir, bool must_be_unexplored ) const;

        std::vector<tripoint_om_omt> place_special(
            const overmap_special &special, const tripoint_om_omt &p, om_direction::type dir,
            const city &cit, bool must_be_unexplored, bool force );

        // DEBUG ONLY!
        void debug_force_add_group( const mongroup &group );
        std::vector<std::reference_wrapper<mongroup>> debug_unsafe_get_groups_at( tripoint_abs_omt &loc );
    private:
        /**
         * Iterate over the overmap and place the quota of specials.
         * If the stated minimums are not reached, it will spawn a new nearby overmap
         * and continue placing specials there.
         * @param enabled_specials specifies what specials to place, and tracks how many have been placed.
         **/
        void place_specials( overmap_special_batch &enabled_specials );
        /**
         * Walk over the overmap and attempt to place specials.
         * @param enabled_specials vector of objects that track specials being placed.
         * @param sectors sectors in which to attempt placement.
         * @param place_optional restricts attempting to place specials that have met their minimum count in the first pass.
         */
        void place_specials_pass( overmap_special_batch &enabled_specials,
                                  om_special_sectors &sectors, bool place_optional, bool must_be_unexplored );

        /**
         * Attempts to place specials within a sector.
         * @param enabled_specials vector of objects that track specials being placed.
         * @param sector sector identifies the location where specials are being placed.
         * @param place_optional restricts attempting to place specials that have met their minimum count in the first pass.
         */
        bool place_special_attempt(
            overmap_special_batch &enabled_specials, const point_om_omt &sector, int sector_width,
            bool place_optional, bool must_be_unexplored );

        void place_mongroups();
        void place_radios();

        void add_mon_group( const mongroup &group );
        void add_mon_group( const mongroup &group, int radius );
        // Spawns a new mongroup (to be called by worldgen code)
        void spawn_mon_group( const mongroup &group, int radius );

        void load_monster_groups( const JsonArray &jsin );
        void load_legacy_monstergroups( const JsonArray &jsin );
        void save_monster_groups( JsonOut &jo ) const;
    public:
        static void load_oter_id_camp_migration( const JsonObject &jo );
        static void load_oter_id_migration( const JsonObject &jo );
        static void reset_oter_id_camp_migrations();
        static void reset_oter_id_migrations();
        static bool oter_id_should_have_camp( const oter_type_str_id &oter );
        static bool is_oter_id_obsolete( const std::string &oterid );
        void migrate_camps( const std::vector<tripoint_abs_omt> &points ) const;
        void migrate_oter_ids( const std::unordered_map<tripoint_om_omt, std::string> &points );
        oter_id get_or_migrate_oter( const std::string &oterid );
};

// A small LRU cache: most oter_id's occur in clumps like forests of swamps.
// This cache helps avoid much more costly lookups in the full hashmap.
struct oter_display_lru {
    static constexpr size_t cache_size = 8; // used below to calculate the next index
    std::array<std::pair<oter_id, oter_t const *>, cache_size> cache;
    size_t cache_next = 0;

    std::pair<std::string, nc_color> get_symbol_and_color( const oter_id &cur_ter, om_vision_level );
};

// "arguments" to oter_symbol_and_color that do not change between calls in a batch
struct oter_display_options {
    struct npc_coloring {
        nc_color color;
        size_t count = 0;
    };

    oter_display_options( tripoint_abs_omt orig, int sight_range ) :
        center( orig ), sight_points( sight_range ) {}

    // Needs a better name
    // Whether or not to draw all sorts of overlays that can blink on the overmap
    bool blink = true;
    // Show debug scent symbols
    bool debug_scent = false;
    // Show mission location by drawing a red background instead of overwriting the tile
    bool hilite_mission = false;
    // Draw the PC location with `hilite()` (blue bg-ish) on terrain at loc instead of '@'
    bool hilite_pc = false;
    // If false, and there is a mission, draw an an indicator towards the mission target on an edge
    bool mission_inbounds = true;
    // Darken explored tiles
    bool show_explored = true;
    bool showhordes = true;
    // Hilight oters revealed by map use
    bool show_map_revealed = false;
    // Draw anything for the PC (assumed to be at center)
    bool show_pc = true;
    // Draw the weather tiles instead of terrain
    bool show_weather = false;

    // Where the PC is/the center of the map
    tripoint_abs_omt center;
    // How far the PC can see
    int sight_points;

    std::optional<tripoint_abs_omt> mission_target = std::nullopt;
    // Locations of NPCs to draw
    std::unordered_map<tripoint_abs_omt, npc_coloring> npc_color;
    // NPC/player paths to draw
    std::unordered_set<tripoint_abs_omt> npc_path_route;
    std::unordered_map<point_abs_omt, int> player_path_route;

    // Zone to draw and location
    std::string sZoneName;
    tripoint_abs_omt tripointZone = tripoint_abs_omt( -1, -1, -1 );

    // Internal bookkeeping value - only draw edge mission indicator once
    mutable bool drawn_mission = false;
};

// arguments for oter_symbol_and_color pertaining to a single point
struct oter_display_args {

    explicit oter_display_args( om_vision_level vis ) : vision( vis ) {}

    // Can the/has the PC seen this tile
    om_vision_level vision;
    // If this tile is on the edge of the drawn tiles, we may draw a mission indicator on it
    bool edge_tile = false;
    // Check if location is within player line-of-sight
    // These ints are treated as unassigned booleans. Use get_and_assign_los() to reference
    // This allows for easy re-use of these variables without the unnecessary lookups if they aren't used
    int los = -1;
    int los_sky = -1;
};

// Symbol and color to display a overmap tile as - depending on notes, overlays, etc...
// args are updated per call, opts are generated before a batch of calls.
// A pointer to lru may be provided for possible speed improvements.
std::pair<std::string, nc_color> oter_symbol_and_color( const tripoint_abs_omt &omp,
        oter_display_args &args, const oter_display_options &opts, oter_display_lru *lru = nullptr );

bool is_river( const oter_id &ter );
bool is_water_body( const oter_id &ter );
bool is_water_body_not_shore( const oter_id &ter );
bool is_lake_or_river( const oter_id &ter );
bool is_ocean( const oter_id &ter );
bool is_road( const oter_id &ter );
bool is_highway( const oter_id &ter );
bool is_highway_reserved( const oter_id &ter );
bool is_highway_special( const oter_id &ter );

/**
* Determine if the provided name is a match with the provided overmap terrain
* based on the specified match type.
* @param name is the name we're looking for.
* @param oter is the overmap terrain id we're comparing our name with.
* @param match_type is the matching rule to use when comparing the two values.
*/
bool is_ot_match( const std::string &name, const oter_id &oter,
                  ot_match_type match_type );

/**
* Gets a collection of sectors and their width for usage in placing overmap specials.
* @param sector_width used to divide the OMAPX by OMAPY map into sectors.
*/
om_special_sectors get_sectors( int sector_width );

/**
* Returns the string of oter without any directional suffix
*/
std::string_view oter_no_dir( const oter_id &oter );

/**
* Returns the string of oter without any directional, connection, or line suffix
*/
std::string_view oter_no_dir_or_connections( const oter_id &oter );

/**
* Return 0, 1, 2, 3 respectively if the suffix is _north, _west, _south, _east
* Return 0 if there's no suffix
*/
int oter_get_rotation( const oter_id &oter );

/**
* Return the directional suffix or "" if there isn't one.
*/
std::string oter_get_rotation_string( const oter_id &oter );
#endif // CATA_SRC_OVERMAP_H
