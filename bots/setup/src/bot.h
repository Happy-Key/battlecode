#pragma once

// Per-champion state survives between turns. Pick one stat and keep buying it.
#include "cambc.h"
#include <__new/placement_new_delete.h>

class Bot {
    int chosen_stat = -1;
    int curr_state = State::FARM;

    // Spend only on power shards of the randomly chosen stat.
    void buy_items();


    enum State{
        FIGHT,
        FARM,
        FLEE,
        FLOCK
    };

    

public:
    static constexpr cambc::vec2 directions[] = {{0,1}, {1,1}, {1,0}, {1,-1}, {0,-1}, {-1,-1}, {-1,0}, {-1,1}};
    // Priorities: shop and recall, attack enemies in range, bank gems, then farm.
    void play_turn();
};
