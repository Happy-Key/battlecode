#include <cambc.h>
#include "draft.h"
#include "bot.h"
#include <algorithm>
#include <cstdint>
#include <string_view>
#include <vector>
#include "random.h"
#include <iostream>


void play_draft() {
    // Choose randomly from the champions our team has not picked.
    const auto chosen = cambc::draft::current_draft(cambc::team::ally);
    std::vector<cambc::champion_draft> available;
    for (int i = 0; i < 11; ++i) { // Released champions only.
        auto pick = static_cast<cambc::champion_draft>(i);
        if (std::find(chosen.begin(), chosen.end(), pick) == chosen.end()) available.push_back(pick);
    }

    // Draft memory starts at byte zero and is copied to each champion.
    auto newPick = available.at(random_number(available.size()));
    cambc::draft::pick(newPick);
    auto draft = chosen;
    draft.push_back(newPick);
    std::vector<std::uint8_t>* id_to_types = new std::vector<std::uint8_t>();
    for (auto d : draft) {
        const std::uint8_t cassi_and_issac_index = 3;
        std::uint8_t champ_type = (std::uint8_t)d;
        champ_type += champ_type > cassi_and_issac_index ? 1 : 0;
        id_to_types->push_back(champ_type);    
        if (champ_type == cassi_and_issac_index) {
            id_to_types->push_back(champ_type + 1);
        }
    }
    id_to_types->insert(id_to_types->cbegin(),(uint8_t)(id_to_types->size()));
    cambc::draft_memory = *id_to_types;
}
