#pragma once

#include <cctype>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace storm
{
inline uint32_t resolveLayerName(std::string name)
{
    static const std::unordered_map<std::string, uint32_t> predefined = {
        {"execute", 0}, {"realize", 1}, {"sea_execute", 2}, {"sea_realize", 3},
        {"interface_execute", 4}, {"interface_realize", 5}, {"iexecute", 4}, {"irealize", 5},
        {"fader_execute", 6}, {"fader_realize", 7}, {"lighter_execute", 8}, {"lighter_realize", 9},
        {"video_execute", 10}, {"video_realize", 11}, {"editor_realize", 12}, {"info_realize", 13},
        {"inf_realize", 13}, {"sound_debug_realize", 14}, {"sea_reflection", 15}, {"sea_reflection2", 16},
        {"sea_sunroad", 17}, {"sun_trace", 18}, {"sails_trace", 19}, {"hull_trace", 20},
        {"mast_island_trace", 21}, {"mast_ship_trace", 22}, {"ship_cannon_trace", 23},
        {"fort_cannon_trace", 24}, {"island_trace", 25}, {"shadow", 26}, {"blood", 27},
        {"rain_drops", 28}, {"net_execute", 29}, {"net_realize", 30},
    };
    static std::unordered_map<std::string, uint32_t> dynamic;
    static uint32_t next = 31;
    for (auto &c : name)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (auto found = predefined.find(name); found != predefined.end())
        return found->second;
    if (auto found = dynamic.find(name); found != dynamic.end())
        return found->second;
    if (name.empty() || next >= sizeof(uint32_t) * 8)
        throw std::runtime_error("No free layer index for " + name);
    const auto index = next++;
    dynamic.emplace(std::move(name), index);
    return index;
}
}
