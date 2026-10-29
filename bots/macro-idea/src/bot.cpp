// Champion decisions. main.cpp dispatches here after the draft.
#include <cambc.h>
#include "bot.h"
#include <array>
#include <iostream>
#include "random.h"
#include <cstdint>
#include <map>
#include <span>
#include <unordered_map>
#include <variant>
#include <utility>
#include <vector>

using cambc::vec2;
using cambc::team;

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

// Compute the attack mask once, rather than once per visible target.
template<class Targets>
bool attack_first_in_range(const Targets& targets) {
    auto& me = cambc::self();
    if (targets.empty() || !me.can_attack()) return false;
    const auto attack_tiles = me.attack_tiles();
    for (const auto& target : targets) {
        if (target.is_visible() && attack_tiles.is_set(target.position()) && target.health() > 0) {
            me.attack(target);
            return true;
        }
    }
    return false;
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
    std::cout << id;
}

void Bot::create_champ_type_id_dicts() {
    std::uint8_t numChamps = reinterpret_cast<const std::uint8_t*>(cambc::draft_memory.data())[0];
    for (int i = 0; i < numChamps; i++) {
        id_to_type[i] = reinterpret_cast<const cambc::champion_type*>(cambc::draft_memory.data())[i+1];
        type_to_id[reinterpret_cast<const cambc::champion_type*>(cambc::draft_memory.data())[i+1]] = i;
    }
}

void Bot::choose_state() {
    auto& me = cambc::self();
    double health_pct = me.health() / me.max_health();
    switch(curr_state) {
        case State::FLEE:
            if (health_pct >= 0.50) curr_state = State::FARM;
            [[__fallthrough__]];
        case State::FARM:
            if (cambc::vision::awake_camp_tiles().is_empty() && me.gem_count() >= 20) curr_state = State::TRIB;
            else if (me.gem_count() > 0) {
                std::uint32_t isTribbing = cambc::shared_memory[0];
                for (cambc::champion champ : cambc::vision::champions()) {
                    if ((isTribbing & (1 << type_to_id[champ.champion_name()])) != 0) curr_state = State::TRIB;
                }
            }
            [[__fallthrough__]];
        case State::TRIB:
            if (me.gem_count() == 0) {
                curr_state = State::FARM;
            }
            [[__fallthrough__]];
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
        case State::FARM:
            farm_camps();
            break;
        case State::TRIB: {
            std::uint32_t isTribbing = 0;
            cambc::shared_memory.read(0, std::span{&isTribbing, 1});
            isTribbing |= 1 << id;
            cambc::shared_memory.write(0, std::span{&isTribbing, 1});
            cambc::bitboard goals;
            goals.clear();
            for (auto altar : cambc::map::altars().positions()) {
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
}
