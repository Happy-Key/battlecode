#include <cambc.h>
#include "draft.h"
#include "bot.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <ostream>
#include <string_view>
#include <set>
#include <vector>
#include "random.h"
#include <iostream>

const int CONTESTED_ZONE_SIZE = 20;
const int CASSI_AND_ISSAC_INDEX = 3;

cambc::bitboard perma_blocked_tiles() {
    auto blocked = cambc::map::walls() | cambc::map::holes() | cambc::map::altars();
    return blocked;
}

cambc::distance_field goal_field(cambc::bitboard goals) {
    cambc::distance_field field;
    if (goals.is_empty()) return field;

    const auto blocked = perma_blocked_tiles();
    // Expand from the goals until our current tile is reached.
    const auto path_walls = blocked & ~goals;
    cambc::bitboard visited;

    field.compute(path_walls, goals,visited);
    return field;
}

bool get_relevant_altars(std::vector<cambc::vec2>& ours , std::vector<cambc::vec2>& contested, int k) {
    cambc::distance_field home = goal_field(cambc::map::spawn_region(cambc::team::ally));
    cambc::distance_field opp = goal_field(cambc::map::spawn_region(cambc::team::enemy));

    for(cambc::vec2 pos : cambc::map::altars().positions()) {
        int diff = opp.get(pos) - home.get(pos);
        if (diff > CONTESTED_ZONE_SIZE) {
            ours.push_back(pos);
        }
        else if (abs(diff) <= CONTESTED_ZONE_SIZE) {
            contested.push_back(pos);
        }
    }

    auto sorter = [home](const cambc::vec2 a, const cambc::vec2 b){
        return home.get(a) < home.get(b) ;
    };

    std::sort(ours.begin(), ours.end(), sorter);
    if (ours.size() > k) ours.erase(ours.begin() + k, ours.end());
    std::sort(contested.begin(), contested.end(), sorter);
    if (contested.size() > k) contested.erase(contested.begin() + k, contested.end());
    return ours.empty();
}

std::vector<std::vector<cambc::vec2>> get_best_camps(cambc::bitboard altars, 
    std::vector<std::pair<std::uint8_t, std::uint8_t>> num_camp_per_type) {
    cambc::distance_field home = goal_field(cambc::map::spawn_region(cambc::team::ally));
    cambc::distance_field altar = goal_field(altars);
    static const auto camp_groups = [home, altar] {
        std::vector<std::tuple<cambc::map::camp_group, std::uint8_t, std::uint8_t>> groups;
        for (const auto& group : cambc::map::camp_groups()) {
            int value = group.gem_value() * 10 / (home.get(group.spawn_tiles().positions()[0]) + altar.get(group.spawn_tiles().positions()[0]));
            int total_health = 0;
            int total_strength = 0;
            for (auto a : group.camps()) {
                auto b = a.base_stats();
                total_health += b.base_health;
                total_strength += b.base_strength;
            }
            int difficulty = total_health * total_strength / 10000;
            groups.emplace_back(group, value, difficulty);
            std::cout << "Value: " << value << ", x: " << group.spawn_tiles().positions()[0].x << ", y: " << group.spawn_tiles().positions()[0].y << std::endl;
        }
        return groups;
    }();
    auto sorter = [](const std::tuple<cambc::map::camp_group, std::uint8_t, std::uint8_t> a, 
                                        const std::tuple<cambc::map::camp_group, std::uint8_t, std::uint8_t> b){
        return std::get<1>(a) > std::get<1>(b);
    };
    auto groups = camp_groups;
    std::vector<std::vector<cambc::vec2>> result(num_camp_per_type.size());
    std::sort(groups.begin(), groups.end(), sorter);
    for (auto a = groups.begin(); a < groups.end(); a++) {
        for (int i = 0; i < num_camp_per_type.size(); i++) {
            if (result[i].size() >= std::get<0>(num_camp_per_type[i])) continue;
            if (std::get<2>(*a) < std::get<1>(num_camp_per_type[i])) {
                auto group = std::get<0>(*a);
                result[i].emplace_back(group.spawn_tiles().positions()[0]);
                break;
            }
        }
    }
    return result;
}

std::vector<std::uint8_t> pick_champions() {
    // Choose randomly from the champions our team has not picked.
    std::vector<std::uint8_t> result;
    std::set<std::uint8_t> seen;
    int max = cambc::draft::team_size();
    while (result.size() < max) {
        std::uint8_t rand = random_number(12);
        if (seen.contains(rand)) continue;
        seen.insert(rand);
        if (rand == CASSI_AND_ISSAC_INDEX) {
            result.push_back(rand);
            result.push_back(rand+1);
            max++;
        }
        if (rand > CASSI_AND_ISSAC_INDEX) {
            result.push_back(rand+1);
        }
        else {
            result.push_back(rand);
        }
    }

    return result;
}

void insert_vector(std::vector<std::uint8_t>& memory, std::vector<std::uint8_t> in,
    std::size_t& offset) {
    memory.insert(memory.begin() + offset, in.size());
    auto end = memory.end();
    memory.insert(end, in.begin(), in.end());
    ++offset;
}

void play_draft() {
    if (cambc::round_number() == 0) {
        std::vector<std::uint8_t> id_buffer = pick_champions();
        std::vector<cambc::vec2> ours, contested, relevant_altars;
        ours.clear();
        contested.clear();
        bool noEZAltars = get_relevant_altars(ours, contested, 3);
        if (noEZAltars) {
            relevant_altars = contested;
        }
        else {
            relevant_altars = ours;
        }
        std::vector<std::uint8_t> altar_buffer;
        for (const auto pos : relevant_altars) {
            altar_buffer.push_back(static_cast<std::uint8_t>(pos.x));
            altar_buffer.push_back(static_cast<std::uint8_t>(pos.y));
        }
        if (cambc::draft::current_draft(cambc::team::ally).size() == 0) {
            for (auto a = altar_buffer.begin(); a < altar_buffer.end(); a += 2) {
                //std::cout << "good altar at x: " << (int)*a << ", y: " << (int)*(a+1) << std::endl;
            }
        }
        
        std::vector<std::uint8_t> camp_buffer;
        for (int i = 0; i < cambc::map::camp_groups().size(); i++) {
            auto c = cambc::map::camp_groups()[i];
            int total_health = 0;
            int total_strength = 0;
            for (int j = 0; j < c.camps().size(); j++) {
                total_health += c.camps()[j].base_stats().base_health;
                total_strength += c.camps()[j].base_stats().base_strength;
            }
            camp_buffer.push_back(std::sqrt(total_health * total_strength / 10000));
            //std::cout << i << ": " << std::sqrt(total_health * total_strength / 10000) << ", ";
        }
        //std::cout << std::endl;
        std::vector<std::uint8_t> memory_buffer;
        std::size_t offset = 0;
        insert_vector(memory_buffer, id_buffer, offset);
        insert_vector(memory_buffer, altar_buffer, offset);
        insert_vector(memory_buffer, camp_buffer, offset);
        memory_buffer.insert(memory_buffer.begin(), (uint8_t)3);
        for (int i = 0; i < 10; i++) {
            //std::cout << (int)*(memory_buffer.begin() + i) << " ";
        }
        //std::cout << std::endl;
        cambc::draft_memory = memory_buffer;
    }

    cambc::draft_memory_buffer memory = cambc::draft_memory_buffer();    
    uint8_t pick = (uint8_t)*(1 + memory.begin() + (int)*memory.begin() + cambc::round_number());
    for (int i = 0; i < cambc::round_number(); i++) {
        if ((uint8_t)memory[1 + (int)memory[0] + i] == CASSI_AND_ISSAC_INDEX) {
            if (i == cambc::round_number() - 1) {
                cambc::draft::pick((cambc::champion_draft)CASSI_AND_ISSAC_INDEX);
                return;
            }
            else {
                pick = (uint8_t)memory[2 + (int)memory[0] + cambc::round_number()];
                break;
            }
        }
    }
    if (pick > CASSI_AND_ISSAC_INDEX) --pick;
    cambc::draft::pick((cambc::champion_draft)pick);
    std::cout << (int)*(1 + memory.begin() + (int)*memory.begin() + cambc::round_number()) << std::endl;
}
