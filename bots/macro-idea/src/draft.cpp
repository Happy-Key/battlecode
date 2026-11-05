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
#include <unordered_set>
#include <vector>
#include "random.h"
#include <iostream>

const int CONTESTED_ZONE_SIZE = 15;
const int CASSI_AND_ISSAC_INDEX = 3;

cambc::bitboard perma_blocked_tiles() {
    auto blocked = cambc::map::walls() | cambc::map::holes();
    return blocked;
}

void goal_field(cambc::bitboard goals, cambc::distance_field& field) {
    if (goals.is_empty()) return;

    const auto blocked = perma_blocked_tiles();
    // Expand from the goals until our current tile is reached.
    const auto path_walls = blocked & ~goals;
    cambc::bitboard visited;

    field.compute(path_walls, goals,visited);
}

bool get_relevant_altars(std::vector<cambc::vec2>& ours , std::vector<cambc::vec2>& contested, int k) {
    cambc::distance_field home;
    goal_field(cambc::map::spawn_region(cambc::team::ally), home);
    cambc::distance_field opp;
    goal_field(cambc::map::spawn_region(cambc::team::enemy), opp);

    for(cambc::vec2 pos : cambc::map::altars().positions()) {
        int diff = opp.get(pos) - home.get(pos);
        if (diff > CONTESTED_ZONE_SIZE) {
            ours.push_back(pos);
        }
        else if (abs(diff) <= CONTESTED_ZONE_SIZE) {
            contested.push_back(pos);
        }
        std::cout << diff << ", ";
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
    cambc::distance_field home;
    goal_field(cambc::map::spawn_region(cambc::team::ally), home);
    cambc::distance_field altar;
    goal_field(altars, altar);
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
            //std::cout << "Value: " << value << ", x: " << group.spawn_tiles().positions()[0].x << ", y: " << group.spawn_tiles().positions()[0].y << std::endl;
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
    std::unordered_set<std::uint8_t> seen = {};
    int max = cambc::draft::team_size();
    while (result.size() < max) {
        std::uint8_t rand = (std::uint8_t)random_number(12);
        if (seen.count(rand) > 0) {
            //std::cout << "dupe" << std::endl;
            continue;
        }
        seen.insert(rand);
        for (auto a : seen) {
            //std::cout << (int)a << " ";
        }
        //std::cout << seen.size() << " ";
        //std::cout << (int)rand << std::endl;
        if (rand == CASSI_AND_ISSAC_INDEX) {
            result.push_back(rand);
            result.push_back(rand+1);
            max++;
        }
        else if (rand > CASSI_AND_ISSAC_INDEX) {
            result.push_back(rand+1);
        }
        else {
            result.push_back(rand);
        }
    }
    for (auto a : result) {
            //std::cout << (int)a << " ";
        }
    //nyeheh i lied no more random drafts
    switch (cambc::draft::team_size()) {
        case 3:
            result = {0, 11, 2};
            break;
        case 4:
            result = {0, 11, 2, 9};
            break;
        case 5:
            result = {0, 11, 2, 9, 10};
            break;
    }
    return result;
}

uint8_t prospector() {
    int gem_value = 0;
    int gold_value = 0;
    int gem_clusters = 0;
    int gold_clusters = 0;
    int total_camps = 0;
    for (auto cg : cambc::map::camp_groups()) {
        gem_value += cg.gem_value();
        gold_value += cg.gold_value();
        for (auto c : cg.camps()) {
            if (c.is_gem_cluster()) gem_clusters++;
            if (c.is_gold_cluster()) gold_clusters++;
            total_camps++;
        }
    }
    if (gem_clusters + gold_clusters > total_camps / 2 || gold_clusters * 20 > gold_value / 2) return 3;
    if (gold_clusters * 20 > gold_value * 4 / 10 || gem_clusters * 7 > gem_value * 7 / 10) return 2;
    if (gold_clusters * 20 > gold_value * 3 / 10 || gem_clusters * 7 > gem_value * 3 / 10) return 1;
    return 0;
}

void insert_vector(std::vector<std::uint8_t>& memory, std::vector<std::uint8_t> in,
    std::size_t& offset) {
    memory.insert(memory.begin() + offset, in.size());
    auto end = memory.end();
    memory.insert(end, in.begin(), in.end());
    ++offset;
}

void insert_item(std::vector<std::uint8_t>& memory, std::uint8_t in,
    std::size_t& offset) {
    memory.insert(memory.begin() + offset, in);
    offset++;
}

void play_draft() {
    if (cambc::round_number() == 0) {
        std::vector<std::uint8_t> id_buffer = pick_champions();
        std::vector<cambc::vec2> ours, contested, relevant_altars;
        ours.clear();
        contested.clear();
        bool noEZAltars = get_relevant_altars(ours, contested, 4);
        if (noEZAltars) {
            relevant_altars = contested;
            std::cout << "contested" << std::endl;
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
                std::cout << "good altar at x: " << (int)*a << ", y: " << (int)*(a+1) << std::endl;
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
            std::cout << i << ": " << std::sqrt(total_health * total_strength / 10000) << ", ";
        }

        std::int8_t flag = 0;
        flag |= prospector() << 1;
        flag |= noEZAltars;
        //std::cout << std::endl;
        std::vector<std::uint8_t> memory_buffer;
        std::size_t offset = 0;
        insert_vector(memory_buffer, id_buffer, offset);
        insert_vector(memory_buffer, altar_buffer, offset);
        insert_vector(memory_buffer, camp_buffer, offset);
        insert_item(memory_buffer, flag, offset);
        memory_buffer.insert(memory_buffer.begin(), (uint8_t)offset);
        for (int i = 0; i < 20; i++) {
            std::cout << (int)*(memory_buffer.begin() + i) << " ";
        }
        //std::cout << std::endl;
        cambc::draft_memory = memory_buffer;
    }

    cambc::draft_memory_buffer memory = cambc::draft_memory_buffer();    
    uint8_t pick = (uint8_t)*(1 + memory.begin() + (int)*memory.begin() + cambc::round_number());
    for (int i = 0; i < cambc::round_number()+1; i++) {
        if ((uint8_t)memory[1 + (int)memory[0] + i] == CASSI_AND_ISSAC_INDEX) {
            if (i == cambc::round_number()) {
                cambc::draft::pick((cambc::champion_draft)CASSI_AND_ISSAC_INDEX);
                return;
            }
            else {
                pick = (uint8_t)*(2 + memory.begin() + (int)*memory.begin() + cambc::round_number());
                break;
            }
        }
    }
    if (pick > CASSI_AND_ISSAC_INDEX) --pick;
    std::cout << (int)pick << std::endl;
    cambc::draft::pick((cambc::champion_draft)pick);
    
}
