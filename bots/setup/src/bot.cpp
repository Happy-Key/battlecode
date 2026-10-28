// Champion decisions. main.cpp dispatches here after the draft.
#include <cambc.h>
#include "bot.h"
#include <array>
#include <iostream>
#include "random.h"
#include <cstdint>
#include <span>
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

int dist_to_closest_goal(vec2 position, cambc::bitboard goals) {
    const auto blocked = blocked_tiles();
    goals &= ~blocked | cambc::vision::camp_tiles();
    if (goals.is_empty())
        return false;

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
    const std::array items{cambc::item_type::power_shard_of_strength,
        cambc::item_type::power_shard_of_focus, cambc::item_type::power_shard_of_health};
    const auto item = items[chosen_stat];
    const unsigned count = me.gold_count() / item.cost();
    if (count && me.can_buy(item, count)) me.buy(item, count);
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
    if (dist_to_closest_goal(me.position(), ally_spawns) <= 5 || attacked) {
        if (me.is_recalling()) me.cancel_recall();
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
    if (dist_to_closest_goal(me.position(), threats) > 5) {
        go_to_spawn(false);
        return;
    }
    cambc::bitboard visited;
    cambc::distance_field field;
    field.compute(blocked_tiles() & ~threats, threats, visited);
    std::optional<vec2> best = std::nullopt;
    int best_score = -1;
    for (auto dir : Bot::directions) {
        vec2 new_position = me.position() + dir;
        if (!cambc::map::is_in_bounds(new_position)) continue;
        if (blocked_tiles().is_set(new_position)) continue;
        int score = 10 * field.get(new_position) - dist_to_closest_goal(new_position, cambc::map::spawn_region(team::ally));
        if (score > best_score) {
            best = dir;
            best_score = score;
        }
    }
    if (best != std::nullopt) me.move(*best); 
    else go_to_spawn(true);
}

float curr_power() {
    auto& me = cambc::self();
    return me.health() * me.strength();
}

void Bot::play_turn() {
    // Initialise this champion's build and read the draft message once.
    if (chosen_stat < 0) {
        chosen_stat = random_number(3);
        std::cout << "draft: " << std::string(reinterpret_cast<const char*>(cambc::draft_memory.data()), 11) << '\n';
    }

    // Read before writing: little-endian u32 at shared-memory byte offset zero.
    std::uint32_t round = 0;
    cambc::shared_memory.read(0, std::span{&round, 1});
    std::cout << "shared round: " << round << '\n';
    round = cambc::round_number();
    cambc::shared_memory.write(0, std::span{&round, 1});

    auto& me = cambc::self();

    // Leave channels running and skip turns where we cannot act.
    if (!me.is_alive() || me.is_recalling() || me.is_submitting_gems() || me.is_teleporting()
        || (!me.can_move() && !me.can_attack()))
        return;

    // Shop before leaving spawn.
    const bool at_spawn = cambc::map::spawn_region(team::ally).is_set(me.position());
    if (at_spawn)
        buy_items();

    // Recall immediately when badly hurt away from spawn.
    if (!at_spawn && me.health() < me.max_health() * 0.35f) {
        me.set_priority(1);
        flee();
        //me.recall();
        
        return;
    }

    // Attack the first enemy champion already in range.
    if (attack_first_in_range(cambc::vision::champions(team::enemy))) return;

    // Bank any carried gems before farming.
    if (me.gem_count() > 0) {
        // Union all altar approach tiles: the field chooses the nearest reachable altar.
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

    farm_camps();

    me.set_priority(1 + random_number(100));
}
