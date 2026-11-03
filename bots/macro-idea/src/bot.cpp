// Champion decisions. main.cpp dispatches here after the draft.
#include <algorithm>
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
    std::vector<vec2> result;
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

bool move_towards_closest(cambc::bitboard goals) {
    auto& me = cambc::self();
    if (!me.can_move() || goals.is_set(me.position()))
        return false;

    // Only free goals or occupied camp tiles can seed the field.
    const auto blocked = blocked_tiles();
    //goals &= ~blocked | cambc::vision::camp_tiles();
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

bool move_randomly() {
    auto me = cambc::self();
    const auto blocked = blocked_tiles();
    auto a = blocked.reachable_tiles(me.position(), me.base_stats().base_move_speed).positions();
    std::cout << "im crazy" << std::endl;
    return me.move_absolute(a[random_number(a.size()) + cambc::round_number() % a.size()]);
}

cambc::bitboard get_closest_pixel_to_bitboard(cambc::bitboard board, cambc::bitboard pixels) {
    cambc::distance_field field;
    cambc::bitboard visited;
    // 1. Compute distances from 'a'
    field.compute(blocked_tiles(), board, visited);

    // 2. Get the raw pointer to the distance values
    const std::uint16_t* dist_ptr = field.ptr();
    std::int32_t map_width = field.width();

    std::optional<cambc::vec2> closest_b_pixel = std::nullopt;
    std::uint16_t min_distance = 0xFFFF;

    // 3. Loop through b's positions using direct array lookup instead of field.get(p)
    for (const cambc::vec2& p : pixels.positions()) {
        if (visited.is_set(p)) {
            // Linear index for row-major order: y * width + x
            std::size_t index = static_cast<std::size_t>(p.y) * map_width + p.x;
            std::uint16_t dist = dist_ptr[index];
            
            if (dist < min_distance) {
                min_distance = dist;
                closest_b_pixel = p;
            }
        }
    }
    cambc::bitboard result;
    result.clear();
    result.set(*closest_b_pixel);
    return result;
}

int stat_power(cambc::champion target) {
    std::cout << "my power: " << std::sqrt(target.max_health() * (target.strength() + target.focus() / 2) / 7000) << std::endl;
    return (int)(std::sqrt(target.max_health() * (target.strength() + target.focus() / 2) / 7000));
}

int curr_power(cambc::champion target) {
    return (int)(std::sqrt(target.health() * (target.strength() + target.focus() / 2) / 7000));
}

void Bot::buy_item(cambc::consumable_type type) {
    cambc::self().buy(type);
    spent_gold += type.cost();
}

void Bot::buy_item(cambc::item_type type, int count) {
    cambc::self().buy(type, count);
    spent_gold += type.cost() * count;
}

void Bot::buy_items() {
    auto& me = cambc::self();

    if (contested) {
        if (me.consumables()[cambc::consumable_type::altar_lock].count() == 0) {
            if (me.gold_count() >= cambc::consumable_type::altar_lock.cost()) buy_item(cambc::consumable_type::altar_lock);
        }
    }
    cambc::item_type item;
    if (build_order_index >= std::size(build_order)){
        item = endgame_build[random_number(std::size(endgame_build))];
    }
    else {
        item = build_order[build_order_index];
    }

    int count = me.gold_count() / item.cost();
    bool rebuy = false;
    if (count + me.item_count()[item] >= 5) {
        count = 5 - me.item_count()[item];
        rebuy = true;
    }
    if (count > 0 && me.can_buy(item, count)) {
        buy_item(item, count);
        //if (!rebuy) set_difficulty_mask(stat_power(me));
    }
    if (rebuy) {
        chosen_stat = random_number(3);
        build_order_index++;
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
    if (me.can_attack() && is_valid_target_in_range(target, attack_tiles)) me.attack(target);
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
void Bot::farm_camps() {
    auto goal_camps = cambc::vision::camps();
    for (int i = 0; i < goal_camps.size(); i++) {
        if (!goal_mask.is_set(goal_camps[i].position())) {
            goal_camps.erase(goal_camps.begin() + i);
            i--;
        }
    }
    if (attack_first_in_range(goal_camps)) return;
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
            && (std::holds_alternative<cambc::map::camp_group_respawning>(group.state())
            ||  std::holds_alternative<cambc::map::camp_group_reward_available>(group.state())))
            goals &= ~spawns;

    // Fresh vision after movement may reveal a camp already in range.
    if (!move_towards_closest(goals & cambc::vision::camp_tiles())) if (move_towards_closest(goals))
        attack_first_in_range(cambc::vision::camps());

    if (cambc::self().can_move()) move_towards_closest(cambc::vision::awake_camp_tiles());
}

void Bot::tribute() {
    auto me = cambc::self();
    cambc::bitboard goals;
    goals.clear();
    cambc::distance_field home;
    cambc::bitboard visited;
    home.compute(blocked_tiles(), cambc::map::spawn_region(team::ally), visited);
    for (auto altar : cambc::map::altars().positions()) {
        if (distance(me.position(), altar) <= cambc::constants::submit_gems_range) {
            if (!cambc::vision::altar_lock_tiles(team::ally).is_set(altar) 
                && me.consumables()[cambc::consumable_type::altar_lock].count() > 0) me.consumables()[cambc::consumable_type::altar_lock].use(altar);
            me.submit_gems(altar);
            return;
        }
        
        if (good_altars.contains(altar) || home.get(altar) < home.get(me.position()) + 5) {
            if (me.consumables()[cambc::consumable_type::altar_lock].count() > 0) {
                auto iter = good_altars.find(altar);
                if (iter != good_altars.end()) {
                    int index = std::distance(good_altars.begin(), iter);
                    int offset = index + id_to_type.size();
                    std::int16_t mem = cambc::shared_memory[0];
                    //cambc::shared_memory.read(0, std::span{&mem, 1});
                    if ((mem & (1 << offset)) != 0) continue;
                }
            }
            goals |= tiles_around(altar, cambc::constants::submit_gems_range);
        }
    }
    if (goals.is_empty()) {
        for (auto altar : cambc::map::altars().positions()) {
            if (good_altars.contains(altar) || home.get(altar) < home.get(me.position())) {
                goals |= tiles_around(altar, cambc::constants::submit_gems_range);
            }
        }
    }
    move_towards_closest(goals);
}

void fight() {
    auto me = cambc::self();
    auto allies = cambc::vision::champions(team::ally);
    auto enemies = cambc::vision::champions(team::enemy);
    if (enemies.empty()) return;
    int lowest = enemies[0].health();
    int target = 0;
    for (int i = 1; i < enemies.size(); i++) {
        if (enemies[i].health() < lowest) {
            lowest = enemies[i].health();
            target = i;
        }
    }
    int enemy_power = 0;
    int ally_power = 0;
    barrage(enemies[target]);
    for (auto e : enemies) {
        barrage(e);
        enemy_power += curr_power(e);
    }
    for (auto a : allies) {
        ally_power += curr_power(a);
    }
    if (ally_power > enemy_power) {
        if (me.can_move()) {
            move_towards_closest(cambc::vision::champion_tiles(team::enemy));
        }
        for (auto a : me.abilities()) {
            if (a.is_on_cooldown()) continue;
            if (a.is_dash()) {
                auto bb = get_closest_pixel_to_bitboard(cambc::vision::champion_tiles(team::enemy), blocked_tiles().reachable_tiles(me.position(), 2));
                a.use(bb.positions()[0]);
            }
        }
        barrage(enemies[target]);
        for (auto e : enemies) {
            barrage(e);
        }
    }
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
    field.compute(blocked_tiles() & ~threats, threats, visited, 5);
    if (move_towards_closest(~visited & ~ blocked_tiles())) return;
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

float gold_value() {
    return 0.03 - cambc::round_number() / 100.0 / 1000.0;
}

float gem_value() {
    int gem_diff = cambc::global_submitted_gems(team::ally) - cambc::global_submitted_gems(team::enemy);
    int x = cambc::global_submitted_gems(team::enemy);
    float urgency = (100 - std::min(gem_diff, 100)) * x / (2000.0 + x);
    return 0.05 + cambc::round_number() / 10.0 / 1000.0 
        + urgency / 1000.0;
}

void Bot::calculate_goal_mask() {
    std::cout << "cgm: disabled ";
    std::vector<int> valid;
    int pow = stat_power(cambc::self());
    for (int mult = 1; mult <= 4; mult *= 2) {
        for (int i = 0; i < get_static_camp_groups().size(); i++) {
            if (camp_group_known_cooldowns[i] > 0) continue;
            if (mult * pow < camp_group_difficulties[i]) continue;
            valid.push_back(i);
            //std::cout << "gotem" << std::endl;
        }
        if (!valid.empty()) break;
        valid.clear();
        std::cout << "empower me" << std::endl;
    }
    cambc::distance_field home;
    cambc::distance_field me;
    cambc::bitboard self;
    self.set(cambc::self().position());
    cambc::bitboard visited;
    cambc::bitboard blocked = cambc::map::walls() | cambc::map::holes() | cambc::map::altars()
        | cambc::map::spawn_region(team::enemy);
    home.compute(blocked, cambc::map::spawn_region(team::ally), visited);
    visited.clear();
    me.compute(blocked, self, visited);
    int best = -1;
    float best_score = 10000;
    for (int i = 0; i < valid.size(); i++) {
        vec2 camp_pos = get_static_camp_groups()[valid[i]].spawn_tiles().positions()[0];
        int gems = get_static_camp_groups()[valid[i]].gem_value();
        int gold = get_static_camp_groups()[valid[i]].gold_value();
        float score = home.get(camp_pos) * 0.1 + me.get(camp_pos) - gem_value() * gems - gold_value() * gold;
        if (score < best_score) {
            best_score = score;
            best = valid[i];
        }
    }
    goal_mask.clear();
    if (best >= 0) {
        auto a = get_static_camp_groups()[best].spawn_tiles();
        goal_mask |= a;
        std::cout << a.positions()[0].x << "," << a.positions()[0].y;
        std::cout << std::endl;
    }
}

void Bot::observe() {
    auto me = cambc::self();
    auto vis = me.visible_tiles();
    for (int i = 0; i < get_static_camp_groups().size(); i++) {        
        if ((camp_spawns[i] & vis).is_empty()) {
            continue;
        }
        auto a = get_static_camp_groups()[i];
        if (a.state().index() == 0) camp_group_known_cooldowns[i] = 0;
        else if (a.state().index() == 1) camp_group_known_cooldowns[i] = a.max_respawn_timer() + 10;
        else if (camp_group_known_cooldowns[i] <= 0) {
            camp_group_known_cooldowns[i] = a.max_respawn_timer();
            std::cout << "Set " << i << " to have cooldown " << a.max_respawn_timer() << std::endl;
            //std::cout << "pos x:  " << a.spawn_tiles().positions()[0].x << ", pos y: " << a.spawn_tiles().positions()[0].y << std::endl;
            //std::cout << "also pos x:  " << get_static_camp_groups()[i].spawn_tiles().positions()[0].x << ", pos y: " << get_static_camp_groups()[i].spawn_tiles().positions()[0].y << std::endl;
        }
    }
    if (contested) {
        for (auto altar : cambc::map::altars().positions()) {
            if (!vis.is_set(altar)) continue;
            auto iter = good_altars.find(altar);
            if (iter == good_altars.end()) continue;
            int index = std::distance(good_altars.begin(), iter);
            int offset = index + id_to_type.size();
            std::int16_t mem;
            cambc::shared_memory.read(0, std::span{&mem, 1});
            if (cambc::vision::altar_lock_tiles(team::ally).is_set(altar)) {
                mem |= 1 << offset;
            }
            else {
                mem &= ~(1 << offset);
            }
            cambc::shared_memory.write(0, std::span{&mem, 1});
        }
    }
}

void Bot::choose_state() {
    auto& me = cambc::self();
    auto vis = me.visible_tiles();
    auto close = tiles_around(me.position(), 3);
    auto kinda_close = tiles_around(me.position(), 5);
    auto blocked = blocked_tiles();
    auto champ_tiles = cambc::vision::champion_tiles();
    auto enemy_tiles = cambc::vision::champion_tiles(team::enemy);
    double health_pct = me.health() / me.max_health();
    int enemy_power = 0;
    for (auto e : cambc::vision::champions(team::enemy)) {
        enemy_power += curr_power(e);
    }
    int ally_power = 0;
    for (auto a : cambc::vision::champions(team::ally)) {
        ally_power += curr_power(a);
    }

    if (health_pct < 0.35) {
        if (!cambc::vision::champions(team::enemy).empty()) curr_state = State::FLEE;
        if (curr_state == State::FARM) {
            if (health_pct < 0.1) {
                curr_state = State::FLEE;
                return;
            }
            int total_health = 0;
            int total_strength = 0;
            for (auto c : cambc::vision::camps()) {
                if (c.is_awake()) {
                    total_health += c.health();
                    total_strength += c.strength();
                }
            }
            if ((int)(std::sqrt(total_health * total_strength / 10000)) > curr_power(me)) curr_state = State::FLEE;
            if (total_health == 0) curr_state = State::CLAIM;
        }
        else if (curr_state == State::FIGHT) {
            if (health_pct < 0.1) {
                curr_state = State::FLEE;
                return;
            }
            if (enemy_power > ally_power) curr_state = State::FLEE;
        }
        else if (curr_state == State::CLAIM) {
            for (int i = 0; i < get_static_camp_groups().size(); i++) {
                if ((camp_spawns[i] & vis).is_empty()) continue;
                if (get_static_camp_groups()[i].state().index() == 1) {
                    if ((camp_spawns[i] & champ_tiles).is_empty()) return;
                }
            }
            curr_state = State::FLEE;
        }
        else curr_state = State::FLEE;
        return;
    }
    if (!(enemy_tiles & kinda_close).is_empty()) curr_state = State::FIGHT;
    switch(curr_state) {
        case State::FLEE:
            if (health_pct >= 0.50) begin_finding();
            break;
        case State::FIND:
            if (!(close & goal_mask & vis).is_empty()) curr_state = State::FARM;
            if (!(cambc::vision::awake_camp_tiles() & close).is_empty()) curr_state = State::FARM;
            for (auto a : get_static_camp_groups()) {
                if ((a.spawn_tiles() & vis).is_empty()) continue;
                if (a.state().index() == 1 && (a.spawn_tiles() & champ_tiles).is_empty()) {
                    curr_state = State::CLAIM;
                    return;
                }
            }
            if (me.gem_count() >= 10) {
                curr_state = State::TRIB;
                return;
            }
            if (goal_mask.is_empty()) curr_state = State::FLEE;
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
                if (!(cambc::vision::asleep_camp_tiles() & close).is_empty()) return;
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
            if (!(cambc::vision::awake_camp_tiles() & close).is_empty()) {
                curr_state = State::FARM;
            }
            
            for (int i = 0; i < get_static_camp_groups().size(); i++) {
                if ((camp_spawns[i] & vis).is_empty()) continue;
                if (get_static_camp_groups()[i].state().index() == 1) {
                    if (camp_spawns[i].is_set(me.position())) return;
                    if (!(camp_spawns[i] & blocked).is_empty()) continue;
                    return;
                }
            }
            if (me.gem_count() >= 10) {
                curr_state = State::TRIB;
                return;
            }
            begin_finding();
            break;
        case State::FIGHT:
            if (enemy_tiles.is_empty()) {
                curr_state = State::FIND;
                return;
            }
            if (enemy_power > ally_power * 1.3) curr_state = State::FLEE;
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
    auto blocked = blocked_tiles();

    if (id < 0) {
        for (int i = 0; i < get_static_camp_groups().size(); i++) {
            camp_spawns.push_back(get_static_camp_groups()[i].spawn_tiles());
        }
        create_champ_type_id_dicts();
        initialize_id();
        auto _ = uint_stream_to_vecs(read_from_draft(1));
        good_altars.insert(_.begin(), _.end());
        camp_group_difficulties = read_from_draft(2);
        for (int i = 0; i < camp_group_difficulties.size(); i++) {
            //std::cout << i << ": " << cambc::map::camp_groups()[i].camps().size() << ", ";
        }
        //std::cout << std::endl;
        camp_group_known_cooldowns.resize(cambc::map::camp_groups().size());
        //set_difficulty_mask(stat_power(me));
        for (auto a : good_altars) {
            std::cout << a.x << ", " << a.y << "/";
        }
        contested = ((std::uint8_t)cambc::draft_memory[4] & 1) == 1;
    }
    if (!me.is_alive()) return;
    for (int i = 0; i < camp_group_known_cooldowns.size(); i++) {
        if (camp_group_known_cooldowns[i] > 0) --camp_group_known_cooldowns[i];
    }
    long fuel = cambc::fuel_remaining();
    observe();
    fuel -= cambc::fuel_remaining();
    std::cout << "Used " << fuel << " fuel." << std::endl;
    
    // Hard skip
    if (!me.can_move() && !me.can_attack())
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
    if (curr_state == State::TRIB) isTribbing |= 1 << id;
    else isTribbing &= ~(1 << id);
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
    for (auto a : me.abilities()) {
        if (a.is_on_cooldown()) continue;
        if (a.is_shield() || a.is_heal()) {
            if (a.can_target_self()) a.use(me);
            if (a.can_target_allies()) {
                int lowest = 100000;
                std::optional<cambc::champion> who = std::nullopt;
                for (auto c : cambc::vision::champions(team::ally)) {
                    if (c == me) continue;
                    if (!a.valid_tiles().is_set(c.position())) continue;
                    if (c.health() < lowest) {
                        if (c.health() == c.max_health()) continue;
                        lowest = c.health();
                        who = c;
                    }
                }
                if (who != std::nullopt) a.use(who.value());
            }
        }
    }
    cambc::bitboard temp;
    temp.clear();
    switch(curr_state) {
        case State::FLEE:
            flee();
            break;
        case State::FIND:
            if (me.gold_count() >= 160 && me.gold_count() > spent_gold / 2) go_to_spawn(false);
            if (!move_towards_closest(goal_mask)) if (!move_randomly()) std::cout << "nooo" << std::endl;
            break;
        case State::FARM:
            farm_camps();
            break;
        case State::TRIB: {
            tribute();
            if (me.can_move()) farm_camps();
            break;
        }
        case State::CLAIM:
            for (int i = 0; i < get_static_camp_groups().size(); i++) {
                if ((camp_spawns[i] & vis).is_empty()) continue;
                if (get_static_camp_groups()[i].state().index() == 1) {
                    if (!(camp_spawns[i] & blocked).is_empty()) continue;
                    temp |= camp_spawns[i];
                }
            }
            move_towards_closest(temp);
            break;
        case State::FIGHT:
            fight();
            break;
        default:
            break;
    }

    std::cout << curr_state << std::endl;
}
