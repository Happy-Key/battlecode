// Champion decisions. main.cpp dispatches here after the draft.
#include <cambc.h>
#include "bot.h"
#include <array>
#include <iostream>
#include "random.h"
#include <cstdint>
#include <iterator>
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

const std::vector<cambc::map::camp_group>& get_static_camp_groups() {
    static const std::vector<cambc::map::camp_group> cached_groups = cambc::map::camp_groups();
    return cached_groups;
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

bool move_to_closest(cambc::bitboard goals) {
    auto& me = cambc::self();
    auto blocked = blocked_tiles();
    //goals &= ~blocked | cambc::vision::camp_tiles();
    const auto path_walls = blocked & ~goals;
    cambc::distance_field field;
    cambc::bitboard visited;
    field.compute_to(path_walls, goals, tiles_around(me.position(), 0), visited);
    auto step = me.position();
    for (auto tile : blocked.reachable_tiles(step, me.base_stats().base_move_speed).positions()) {
        if (field.get(tile) < field.get(step))  {
            step = tile;
            break;
        }
        if (field.get(tile) == field.get(step)) {
            step = tile;
        }
    }
    return step != me.position() && me.move_absolute(step);
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

int curr_power(cambc::champion target) {
    std::cout << "my power: " << std::sqrt(target.max_health() * target.strength() / 5000) << std::endl;
    return (int)(std::sqrt(target.max_health() * target.strength() / 5000));
}

void Bot::buy_items() {
    auto& me = cambc::self();
    const std::array items{cambc::item_type::bruiser_shard_of_strength,
        cambc::item_type::bruiser_shard_of_health, cambc::item_type::bruiser_shard_of_focus};
    const auto item = items[chosen_stat];
    int count = me.gold_count() / item.cost();
    bool rebuy = false;
    if (count + me.item_count()[items[chosen_stat]] > 5) {
        count = 5 - me.item_count()[items[chosen_stat]];
        rebuy = true;
    }
    if (count > 0 && me.can_buy(item, count)) {
        me.buy(item, count);
        if (!rebuy) set_difficulty_mask(curr_power(me));
    }
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

void Bot::begin_finding() {
    calculate_goal_mask();
    curr_state = State::FIND;
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
        std::cout << (int)chosen_champ_types[i] << ", ";
        id_to_type[i] = (cambc::champion_type)chosen_champ_types[i];
        type_to_id[(cambc::champion_type)chosen_champ_types[i]] = i;
    }
    std::cout << std::endl;
}

void Bot::set_difficulty_mask(int power) {
    std::vector<cambc::map::camp_group> valid;
    std::cout << "manageable: ";
    for (int i = 0; i < camp_group_difficulties.size(); i++) {
        if (power >= camp_group_difficulties[i]) {
            valid.push_back(cambc::map::camp_groups()[i]);
            std::cout << i << ", ";
        }
    }
    std::cout << std::endl;
    difficulty_mask.clear();
    for (auto a : valid) {
        difficulty_mask |= a.spawn_tiles();
    }
}

void Bot::calculate_goal_mask() {
    std::cout << "cgm: disabled ";
    goal_mask = difficulty_mask;
    for (int i = 0; i < camp_group_known_cooldowns.size(); i++) {
        if (camp_group_known_cooldowns[i] > 0) {
            goal_mask &= ~cambc::map::camp_groups()[i].spawn_tiles();
            std::cout << i << ", ";
        }
    }
    std::cout << std::endl;
}

void Bot::observe() {
    auto me = cambc::self();
    auto vis = me.visible_tiles();
    int i = 0;
    for (int i = 0; i < get_static_camp_groups().size(); i++) {        
        if ((camp_spawns[i] & vis).is_empty()) {
            continue;
        }
        auto a = get_static_camp_groups()[i];
        if (a.state().index() == 0) camp_group_known_cooldowns[i] = 0;
        else if (camp_group_known_cooldowns[i] <= 0) {
            camp_group_known_cooldowns[i] = a.max_respawn_timer();
            std::cout << "Set " << i << " to have cooldown " << a.max_respawn_timer() << std::endl;
            std::cout << "pos x:  " << a.spawn_tiles().positions()[0].x << ", pos y: " << a.spawn_tiles().positions()[0].y << std::endl;
            std::cout << "also pos x:  " << get_static_camp_groups()[i].spawn_tiles().positions()[0].x << ", pos y: " << get_static_camp_groups()[i].spawn_tiles().positions()[0].y << std::endl;
        }
        i++;
    }
}

void Bot::choose_state() {
    auto& me = cambc::self();
    auto vis = me.visible_tiles();
    double health_pct = me.health() / me.max_health();
    if (health_pct < 0.35) {
        curr_state = State::FLEE;
        return;
    }
    int multiplier = 2;
    switch(curr_state) {
        case State::FLEE:
            if (health_pct >= 0.50) begin_finding();
            break;
        case State::FIND:
            if (!(vis & goal_mask).is_empty()) curr_state = State::FARM;
            if (goal_mask.is_empty()) {
                int total_power = curr_power(me);
                for (cambc::champion c : cambc::vision::champions(team::ally)) {
                    total_power += curr_power(c);
                }
                set_difficulty_mask(total_power);
                calculate_goal_mask();
            }
            while (goal_mask.is_empty()) {
                set_difficulty_mask(curr_power(me) * multiplier);
                goal_mask = difficulty_mask;
                multiplier *= 2;
            }
            break;
        case State::FARM:
            if (cambc::vision::awake_camp_tiles().is_empty()) {
                for (auto a : get_static_camp_groups()) {
                    if ((a.spawn_tiles() & vis).is_empty()) continue;
                    if (a.state().index() == 1) {
                        curr_state = State::CLAIM;
                        return;
                    }
                }
                if (me.gem_count() >= 10) {
                    curr_state = State::TRIB;
                    return;
                }
                begin_finding();
            }
            if (me.gem_count() > 0) {
                std::uint32_t isTribbing = cambc::shared_memory[0];
                for (cambc::champion champ : cambc::vision::champions()) {
                    if ((isTribbing & (1 << type_to_id[champ.champion_name()])) != 0) {
                        curr_state = State::TRIB;
                        return;
                    }
                }
            }
            break;
        case State::TRIB:
            if (me.gem_count() == 0) {
                begin_finding();
            }
            break;
        case State::CLAIM:
            if (!cambc::vision::awake_camp_tiles().is_empty()) {
                curr_state = State::FARM;
            }
            
            for (int i = 0; i < get_static_camp_groups().size(); i++) {
                if ((camp_spawns[i] & vis).is_empty()) continue;
                if (get_static_camp_groups()[i].state().index() == 1) {
                    return;
                }
            }
            begin_finding();
            break;
        default:
            if (health_pct < 0.35) {
                curr_state = State::FLEE;
                break;
            }
    }
}

void Bot::play_turn() {
    auto& me = cambc::self();
    auto vis = me.visible_tiles();

    if (id < 0) {
        for (int i = 0; i < get_static_camp_groups().size(); i++) {
            camp_spawns.push_back(get_static_camp_groups()[i].spawn_tiles());
        }
        create_champ_type_id_dicts();
        initialize_id();
        good_altars = uint_stream_to_vecs(read_from_draft(1));
        camp_group_difficulties = read_from_draft(2);
        for (int i = 0; i < camp_group_difficulties.size(); i++) {
            std::cout << i << ": " << cambc::map::camp_groups()[i].camps().size() << ", ";
        }
        std::cout << std::endl;
        camp_group_known_cooldowns.resize(cambc::map::camp_groups().size());
        set_difficulty_mask(curr_power(me));
    }
    
    for (int i = 0; i < camp_group_known_cooldowns.size(); i++) {
        if (camp_group_known_cooldowns[i] > 0) --camp_group_known_cooldowns[i];
    }
    long fuel = cambc::fuel_remaining();
    observe();
    fuel -= cambc::fuel_remaining();
    std::cout << "Used " << fuel << " fuel." << std::endl;
    
    // Hard skip
    if (!me.is_alive() || (!me.can_move() && !me.can_attack()))
        return;
    //Soft skip (should consider whether to cancel)
    if (me.is_recalling() || me.is_submitting_gems() || me.is_teleporting())
        return;
    
    // Shop before leaving spawn.
    const bool at_spawn = cambc::map::spawn_region(team::ally).is_set(me.position());
    if (at_spawn) {
        buy_items();
        
    }
    choose_state();

    std::uint32_t isTribbing = 0;
    cambc::shared_memory.read(0, std::span{&isTribbing, 1});
    isTribbing &= ~(1 << id);
    cambc::shared_memory.write(0, std::span{&isTribbing, 1});

    if (curr_state != State::FLEE) {
        auto enemies = cambc::vision::champions(team::enemy);
        if (!enemies.empty()) {
            int lowest = enemies[0].health();
            int target = 0;
            for (int i = 1; i < enemies.size(); i++) {
                if (enemies[i].health() < lowest) {
                    lowest = enemies[i].health();
                    target = i;
                }
            }
            barrage(enemies[target]);
        }
    }
    cambc::bitboard temp;
    temp.clear();
    switch(curr_state) {
        case State::FLEE:
            flee();
            break;
        case State::FIND:
            if (!move_towards_closest(goal_mask)) std::cout << "nooo" << std::endl;
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
            for (auto altar : good_altars) {
                if (distance(me.position(), altar) <= cambc::constants::submit_gems_range
                    && !cambc::vision::has_altar_lock(altar)) {
                        me.submit_gems(altar);
                        return;
                    }
                if (!cambc::vision::has_altar_lock(altar)) goals |= tiles_around(altar, cambc::constants::submit_gems_range);
            }
            move_towards_closest(goals);
            break;
        }
        case State::CLAIM:
            for (int i = 0; i < get_static_camp_groups().size(); i++) {
                if ((camp_spawns[i] & vis).is_empty()) continue;
                if (get_static_camp_groups()[i].state().index() == 1) {
                    move_towards_closest(camp_spawns[i]);
                    break;
                }
            }
            
            break;
        default:
            break;
    }

    std::cout << curr_state << std::endl;
}
