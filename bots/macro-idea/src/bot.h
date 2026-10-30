#pragma once

#include "cambc.h"
#include <cstdint>
#include <optional>
#include <vector>
// Per-champion state survives between turns. Pick one stat and keep buying it.
class Bot {
    int chosen_stat = 0;
    int curr_state = State::FARM;
    int rotation = 0;
    int id = -1;
    
    void buy_items();

public:
    enum State {
        FLEE,
        FARM,
        TRIB,
        FIND,
        CLAIM,
    };
    static constexpr cambc::vec2 directions[] = {{0,1}, {1,1}, {1,0}, {1,-1}, {0,-1}, {-1,-1}, {-1,0}, {-1,1}};
    std::unordered_map<std::uint8_t, cambc::champion_type> id_to_type;
    std::unordered_map<cambc::champion_type, std::uint8_t> type_to_id;
    std::vector<cambc::vec2> good_altars;
    std::vector<uint8_t> camp_group_known_cooldowns;
    std::vector<uint8_t> camp_group_difficulties;
    std::vector<cambc::bitboard> camp_spawns;
    cambc::bitboard difficulty_mask;
    cambc::bitboard goal_mask;
    
    void play_turn();
    void choose_state();
    void initialize_id();
    void create_champ_type_id_dicts();
    void set_difficulty_mask(int power);
    void calculate_goal_mask();
    void begin_finding();
    void observe();
};