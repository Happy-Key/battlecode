// Champion decisions. main.cpp dispatches here after the draft.
#include <cambc.h>
#include "bot.h"
#include <array>
#include <iostream>
#include "random.h"
#include <cstdint>
#include <map>
#include <optional>
#include <ostream>
#include <span>
#include <unordered_map>
#include <variant>
#include <utility>
#include <vector>

using cambc::vec2;
using cambc::team;

std::vector<std::uint8_t> read_from_draft(int vec_index) {
    cambc::draft_memory_buffer memory = cambc::draft_memory;    
    
    std::uint8_t num_vec = static_cast<std::uint8_t>(memory[0]);
    std::size_t offset = 1 + num_vec;
    for (std::uint8_t i = 0; i < vec_index; i++) {
        offset += (std::uint8_t)memory[1 + i];
    }
    std::size_t length = (int)memory[1 + vec_index];
    std::vector<std::uint8_t> result;
    for (auto a = memory.begin() + offset; a < memory.begin() + offset + length; a++) {
        result.push_back((std::uint8_t)*a);
    }

    return result;
}

std::vector<vec2> uint_stream_to_vecs(std::vector<std::uint8_t> stream) {
    std::vector<vec2> result(stream.size() / 2);
    for (auto a = stream.begin(); a < stream.end(); a += 2) {
        result.push_back(vec2((int)*a,(int)*(a+1)));
    }
    return result;
}

std::vector<std::vector<cambc::vec2>> uint_stream_to_camps(std::vector<std::uint8_t> stream) {
    int num_categories = stream[0];
    std::vector<int> cat_sizes(num_categories);
    int offset = 1;
    for (int i = 0; i < num_categories; i++) {
        cat_sizes[i] = stream[offset];
        offset += 1;
    }
    std::vector<std::vector<cambc::vec2>> result(num_categories);
    for (int i = 0; i < num_categories; i++) {
        for (int j = 0; j < cat_sizes[i]; j++) {
            result[i].push_back(cambc::vec2(stream[offset],stream[offset+1]));
            offset += 2;
        }
    }
    return result;
}

int distance(vec2 a, vec2 b) {
    return static_cast<int>(a.chebyshev_distance(b));
}

cambc::bitboard tiles_around(vec2 position, int radius) {
    cambc::bitboard tiles;
    tiles.clear();
    tiles.draw_chebyshev(position, radius);
    return tiles;
}

// Combine terrain and visible occupants using native bitboard operations.
cambc::bitboard blocked_tiles() {
    auto blocked = cambc::map::walls() | cambc::map::holes() | cambc::map::altars()
        | cambc::map::spawn_region(team::enemy) | cambc::vision::champion_tiles()
        | cambc::vision::camp_tiles() | cambc::vision::teleport_tiles();
    blocked.set(cambc::self().position(), false);
    return blocked;
}

int dist_to_closest(vec2 position, cambc::bitboard goals) {
    if (goals.is_empty())
        return false;
    const auto blocked = blocked_tiles();
    // Expand from the goals until our current tile is reached.
    const auto path_walls = blocked & ~goals;
    cambc::distance_field field;
    cambc::bitboard visited;
    field.compute_to(path_walls, goals, tiles_around(position, 0), visited);
    return field.get(position);
}

bool move_towards_closest(cambc::bitboard goals) {
    auto& me = cambc::self();
    if (!me.can_move() || goals.is_set(me.position()))
        return false;

    // Only free goals or occupied camp tiles can seed the field.
    const auto blocked = blocked_tiles();
    goals &= ~blocked | cambc::vision::camp_tiles();
    if (goals.is_empty())
        return false;
    if (goals.count() == 1 && !blocked.is_set(goals.positions()[0]))
        return me.move_towards(goals.positions()[0]);

    // Expand from the goals until our current tile is reached.
    const auto path_walls = blocked & ~goals;
    cambc::distance_field field;
    cambc::bitboard visited;
    field.compute_to(path_walls, goals, tiles_around(me.position(), 0), visited);

    // Choose a reachable step with a smaller distance to the goal.
    auto step = me.position();
    for (auto tile : blocked.reachable_tiles(step, me.base_stats().base_move_speed).positions())
        if (field.get(tile) < field.get(step)) step = tile;

    return step != me.position() && me.move_absolute(step);
}

void Bot::buy_items() {
    auto& me = cambc::self();
    const std::array items{cambc::item_type::bruiser_shard_of_strength,
        cambc::item_type::bruiser_shard_of_health, cambc::item_type::bruiser_shard_of_focus};
    const auto item = items[chosen_stat];
    int count = me.gold_count() / item.cost();
    bool rebuy = false;
    if (count + me.item_count()[items[chosen_stat]] >= 5) {
        count = 5 - me.item_count()[items[chosen_stat]];
        rebuy = true;
    }
    if (count > 0 && me.can_buy(item, count)) me.buy(item, count);
    if (rebuy) {
        chosen_stat = random_number(3);
        if (count > 0) buy_items();
    }
}

template<class Target>
bool is_valid_target_in_range(const Target& target, cambc::bitboard range) {
    return target.is_visible() && range.is_set(target.position()) && target.health() > 0;
}

template<class Target>
void barrage(const Target& target) {
    auto& me = cambc::self();
    cambc::bitboard attack_tiles = me.attack_tiles();
    if (is_valid_target_in_range(target, attack_tiles)) me.attack(target);
    for (auto a : me.abilities()) {
        if (a.is_on_cooldown()) continue;
        if (a.is_damage() || a.is_cc() || a.is_aoe_damage()) {
            cambc::bitboard tiles_in_range = a.valid_tiles();
            if (is_valid_target_in_range(target, tiles_in_range)) {
                if (a.can_target_enemies() || a.can_target_camps()) a.use(target);
                if (a.can_target_ground()) a.use(target.position());
                return;
            }
        }
        if (a.is_attack_reset()) {
            if (is_valid_target_in_range(target, attack_tiles)) {
                a.use(me);
                me.attack(target);
                return;
            }
        }
    }
}

// Compute the attack mask once, rather than once per visible target.
template<class Targets>
bool attack_first_in_range(const Targets& targets) {
    auto& me = cambc::self();
    if (targets.empty() || !me.can_attack()) return false;
    const auto attack_tiles = me.attack_tiles();
    for (const auto& target : targets) {
        if (target.is_visible() && attack_tiles.is_set(target.position()) && target.health() > 0) {
            barrage(target);
            return true;
        }
    }
    return false;
}

int curr_power() {
    auto me = cambc::self();
    return me.health() * me.strength() / 2000;
}

cambc::bitboard Bot::target_camps_bitboard(int power) {
    int i = difficulties.size() - 1;
    for (; i >= 0; i--) {
        if (power >= difficulties[i]) break;
    }
    cambc::bitboard result;
    result.clear();
    for (auto a : target_camps[i]) {
        result.set(a);
    }
    return result;
}

void Bot::find_target_camp() {
    auto me = cambc::self();
    auto possible_targets = target_camps_bitboard(curr_power());
    if (possible_targets.is_empty()) {
        curr_target = std::nullopt;
        return;
    }
    int num_targets = possible_targets.positions().size();
    curr_target = possible_targets.positions()[rotation % num_targets];
    rotation += random_number(4);
}

// Attack a camp already in range, otherwise move toward the nearest camp tiles.
void farm_camps() {
    if (attack_first_in_range(cambc::vision::camps())) return;

    // Cache fixed spawn locations; only visible respawn state changes each turn.
    static const auto camp_groups = [] {
        std::vector<std::pair<cambc::map::camp_group, cambc::bitboard>> groups;
        for (const auto& group : cambc::map::camp_groups()) groups.emplace_back(group, group.spawn_tiles());
        return groups;
    }();
    static const auto camp_spawns = [] {
        cambc::bitboard tiles;
        tiles.clear();
        for (const auto& [group, spawns] : camp_groups) tiles |= spawns;
        return tiles;
    }();
    auto goals = camp_spawns;
    const auto visible = cambc::vision::visible_tiles();
    for (const auto& [group, spawns] : camp_groups)
        if (!(spawns & visible).is_empty()
            && std::holds_alternative<cambc::map::camp_group_respawning>(group.state()))
            goals &= ~spawns;

    // Fresh vision after movement may reveal a camp already in range.
    if (move_towards_closest(goals))
        attack_first_in_range(cambc::vision::camps());

    if (cambc::self().can_move()) move_towards_closest(cambc::vision::awake_camp_tiles());
}

void go_to_spawn(bool attacked) {
    auto& me = cambc::self();
    cambc::bitboard ally_spawns = cambc::map::spawn_region(team::ally);
    if (dist_to_closest(me.position(), ally_spawns) <= 5 || attacked) {
        move_towards_closest(ally_spawns);
    }
    else {
        me.recall();
    }
}

void flee() {
    auto me = cambc::self();
    auto threats = cambc::vision::champion_tiles(team::enemy) | cambc::vision::awake_camp_tiles();
    if (threats.is_empty()) {
        go_to_spawn(false);
        return;
    }
    cambc::bitboard visited;
    cambc::distance_field field;
    field.compute(blocked_tiles() & ~threats, threats, visited);
    std::optional<vec2> best = std::nullopt;
    int base = field.get(me.position());
    for (auto dir : Bot::directions) {
        vec2 new_position = me.position() + dir;
        if (!cambc::map::is_in_bounds(new_position)) continue;
        if (blocked_tiles().is_set(new_position)) continue;
        if (field.get(new_position) > base) {
            me.move(dir);
            return;
        }
    }
    go_to_spawn(true);
}

void Bot::initialize_id() {
    auto& me = cambc::self();
    id = type_to_id[me.champion_name()];
    std::cout << "My id is: " << id << std::endl;
}

void Bot::create_champ_type_id_dicts() {
    std::vector<std::uint8_t> chosen_champ_types = read_from_draft(0);
    for (int i = 0; i < chosen_champ_types.size(); i++) {
        id_to_type[i] = (cambc::champion_type)chosen_champ_types[i];
        type_to_id[(cambc::champion_type)chosen_champ_types[i]] = i;
    }
}

void Bot::choose_state() {
    auto& me = cambc::self();
    double health_pct = me.health() / me.max_health();
    if (health_pct < 0.35) {
        curr_state = State::FLEE;
        return;
    }
    switch(curr_state) {
        case State::FLEE:
            if (health_pct >= 0.50) curr_state = State::FIND;
            break;
        case State::FIND:
            if (curr_target.has_value()) {if (cambc::vision::in_vision(curr_target.value())) curr_state = FARM;}
            else curr_state = FARM;
            break;
        case State::FARM:
            if (cambc::vision::awake_camp_tiles().is_empty() && me.gem_count() >= 20) {
                curr_state = State::TRIB;
                return;
            }
            else if (me.gem_count() > 0) {
                std::uint32_t isTribbing = cambc::shared_memory[0];
                for (cambc::champion champ : cambc::vision::champions()) {
                    if ((isTribbing & (1 << type_to_id[champ.champion_name()])) != 0) {
                        curr_state = State::TRIB;
                        return;
                    }
                }
            }
            if (!cambc::vision::camp_groups().empty() && curr_target.has_value()) {
                for (auto a : cambc::vision::camp_groups()) {
                    if (a.state().index() != 2) {
                        if (a.spawn_tiles().is_set(curr_target.value())) return;
                    }
                }
                curr_state = State::FIND;
                return;
            }
            else curr_state = State::FIND;
            break;
        case State::TRIB:
            if (me.gem_count() == 0) {
                curr_state = State::FIND;
            }
            break;
        default:
            if (health_pct < 0.35) {
                curr_state = State::FLEE;
                break;
            }
    }
}

void Bot::play_turn() {
    if (id < 0) {
        create_champ_type_id_dicts();
        initialize_id();
        good_altars = uint_stream_to_vecs(read_from_draft(1));
        target_camps = uint_stream_to_camps(read_from_draft(2));
    }
    auto& me = cambc::self();

    // Hard skip
    if (!me.is_alive() || !me.can_move() && !me.can_attack())
        return;
    //Soft skip (should consider whether to cancel)
    if (me.is_recalling() || me.is_submitting_gems() || me.is_teleporting())
        return;
    
    // Shop before leaving spawn.
    const bool at_spawn = cambc::map::spawn_region(team::ally).is_set(me.position());
    if (at_spawn)
        buy_items();

    choose_state();

    std::uint32_t isTribbing = 0;
    cambc::shared_memory.read(0, std::span{&isTribbing, 1});
    isTribbing &= ~(1 << id);
    cambc::shared_memory.write(0, std::span{&isTribbing, 1});

    switch(curr_state) {
        case State::FLEE:
            flee();
            break;
        case State::FIND:
            if (!curr_target.has_value())find_target_camp();
            if (curr_target.has_value()) me.move_towards(curr_target.value());
            else farm_camps();
            break;
        case State::FARM:
            farm_camps();
            if (curr_target.has_value()) {
                for (auto a : cambc::vision::camp_groups()) {
                    if (a.spawn_tiles().is_set(curr_target.value())) {
                        if (a.state().index() == 2) curr_target = std::nullopt;
                    }
                }
            }
            break;
        case State::TRIB: {
            std::uint32_t isTribbing = 0;
            cambc::shared_memory.read(0, std::span{&isTribbing, 1});
            isTribbing |= 1 << id;
            cambc::shared_memory.write(0, std::span{&isTribbing, 1});
            cambc::bitboard goals;
            goals.clear();
            for (auto altar : good_altars) {
                if (distance(me.position(), altar) <= cambc::constants::submit_gems_range
                    && !cambc::vision::has_altar_lock(altar)) {
                        me.submit_gems(altar);
                        return;
                    }
                if (!cambc::vision::has_altar_lock(altar)) goals |= tiles_around(altar, cambc::constants::submit_gems_range);
            }
            move_towards_closest(goals);
            return;
        }
        default:
            break;
    }

    std::cout << curr_state << std::endl;
}
