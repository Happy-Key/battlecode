#pragma once

#include "cambc.h"
#include <cstdint>
#include <optional>
#include <queue>
#include <set>
#include <vector>
// Per-champion state survives between turns. Pick one stat and keep buying it.
class Bot {
    int chosen_stat = 0;
    int curr_state = State::FARM;
    int rotation = 0;
    int id = -1;
    bool contested = false;
    int spent_gold = 0;
    int build_order_index = 0;
    std::vector<cambc::item_type> build_order = {cambc::item_type::jungle_shard_of_strength, 
        cambc::item_type::treasure_shard_of_health, cambc::item_type::monster_shard_of_strength, cambc::item_type::durability_shard_of_health,
        cambc::item_type::collectors_shard_of_strength, cambc::item_type::skirmish_shard_of_health};
    std::vector<cambc::item_type> endgame_build = {cambc::item_type::bruiser_shard_of_health, cambc::item_type::bruiser_shard_of_strength};
    
    void buy_items();

public:
    enum State {
        FLEE,
        FARM,
        TRIB,
        FIND,
        CLAIM,
        FIGHT,
    };
    static constexpr cambc::vec2 directions[] = {{0,1}, {1,1}, {1,0}, {1,-1}, {0,-1}, {-1,-1}, {-1,0}, {-1,1}};
    std::unordered_map<std::uint8_t, cambc::champion_type> id_to_type;
    std::unordered_map<cambc::champion_type, std::uint8_t> type_to_id;
    std::set<cambc::vec2> good_altars;
    std::vector<uint8_t> camp_group_known_cooldowns;
    std::vector<uint8_t> camp_group_difficulties;
    std::vector<cambc::bitboard> camp_spawns;
    std::vector<bool> camp_group_in_view;
    std::queue<uint8_t> message_queue;
    cambc::bitboard difficulty_mask;
    cambc::bitboard goal_mask;
    
    void play_turn();
    void choose_state();
    void initialize_id();
    void create_champ_type_id_dicts();
    void farm_camps();
    void set_difficulty_mask(int power);
    void calculate_goal_mask();
    void begin_finding();
    void observe();
    void tribute();
    void buy_item(cambc::consumable_type);
    void buy_item(cambc::item_type, int count);
};