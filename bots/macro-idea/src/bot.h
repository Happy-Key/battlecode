#pragma once

#include "cambc.h"
// Per-champion state survives between turns. Pick one stat and keep buying it.
class Bot {
    int chosen_stat = 0;
    int curr_state = State::FARM;
    int id = -1;
    
    void buy_items();

public:
    enum State {
        FLEE,
        FARM,
        TRIB,
        FIND,
    };
    static constexpr cambc::vec2 directions[] = {{0,1}, {1,1}, {1,0}, {1,-1}, {0,-1}, {-1,-1}, {-1,0}, {-1,1}};
    std::unordered_map<std::uint8_t, cambc::champion_type> id_to_type;
    std::unordered_map<cambc::champion_type, std::uint8_t> type_to_id;
    
    void play_turn();
    void choose_state();
    void initialize_id();
    void create_champ_type_id_dicts();
};
