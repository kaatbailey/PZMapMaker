// groundpalette.cpp — see groundpalette.hpp for the survey this encodes and
// for the floating-point note.

#include "groundpalette.hpp"

#include <cstdio>
#include <stdexcept>

namespace pzformat {

const std::array<GroundPalette::BaseGroup, 3>& GroundPalette::groups() {
    static const std::array<BaseGroup, 3> kGroups{{
        {{16, 21, 22, 23}, 58.6, 0.606, "grass, dense"},
        {{32, 37, 38, 39}, 17.4, 0.370, "grass, medium"},
        {{48, 53, 54, 55},  8.1, 0.152, "grass, light"},
    }};
    return kGroups;
}

GroundPalette::GroundPalette(const std::vector<BaseGroup>& keptGroups,
                             const std::vector<std::string>& keptTufts)
    : groups_(keptGroups), tufts_(keptTufts) {

    groupCumulative_.resize(groups_.size());
    double run = 0;
    for (std::size_t i = 0; i < groups_.size(); i++) {
        run += groups_[i].weight;
        groupCumulative_[i] = run;
    }

    tuftCumulative_.resize(tufts_.size());
    run = 0;
    for (std::size_t i = 0; i < tufts_.size(); i++) {
        run += rowWeightOf(tufts_[i]);
        tuftCumulative_[i] = run;
    }

    for (const BaseGroup& g : groups_) {
        for (const int idx : g.indices) {
            all.push_back(std::string(BASE_SHEET) + std::to_string(idx));
        }
    }
    for (const std::string& t : tufts_) all.push_back(t);
}

GroundPalette GroundPalette::pick(const TileIndex& ti,
                                  const std::unordered_set<std::string>& sprites) {
    std::vector<BaseGroup> keptGroups;
    std::vector<std::string> dropped;

    for (const BaseGroup& g : groups()) {
        bool ok = true;
        for (const int idx : g.indices) {
            const std::string n = std::string(BASE_SHEET) + std::to_string(idx);
            if (ti.get(n) == nullptr || sprites.find(n) == sprites.end()) {
                ok = false;
                dropped.push_back(n);
            }
        }
        if (ok) keptGroups.push_back(g);
    }

    std::vector<std::string> keptTufts;
    for (std::size_t row = 0; row < TUFT_ROW_WEIGHT.size(); row++) {
        for (int col = 0; col < TUFT_USABLE_COLS; col++) {
            const std::string n = std::string(TUFT_SHEET)
                + std::to_string(static_cast<int>(row) * TUFT_ROW_WIDTH + col);
            if (ti.get(n) != nullptr && sprites.find(n) != sprites.end()) {
                keptTufts.push_back(n);
            }
        }
    }

    if (keptGroups.empty()) {
        // Java: IllegalStateException, message includes the dropped list.
        std::string msg = "GroundPalette: no usable base ground group. Missing: [";
        for (std::size_t i = 0; i < dropped.size(); i++) {
            if (i > 0) msg += ", ";
            msg += dropped[i];
        }
        msg += "]";
        throw std::runtime_error(msg);
    }
    if (!dropped.empty()) {
        std::printf("ground palette: dropped %zu unusable base tiles\n", dropped.size());
    }
    return GroundPalette(keptGroups, keptTufts);
}

double GroundPalette::rowWeightOf(const std::string& tuftName) {
    const std::size_t prefix = std::string(TUFT_SHEET).size();
    const int idx = std::stoi(tuftName.substr(prefix));
    const int row = idx / TUFT_ROW_WIDTH;
    return row < static_cast<int>(TUFT_ROW_WEIGHT.size())
               ? TUFT_ROW_WEIGHT[static_cast<std::size_t>(row)]
               : 0.1;
}

GroundPalette::Ground GroundPalette::roll(JavaRandom& rng) const {
    const double r = rng.nextDouble() * groupCumulative_[groupCumulative_.size() - 1];
    std::size_t gi = 0;
    while (gi < groupCumulative_.size() - 1 && r > groupCumulative_[gi]) {
        gi++;
    }
    const BaseGroup& g = groups_[gi];
    const std::string base = std::string(BASE_SHEET)
        + std::to_string(g.indices[static_cast<std::size_t>(
            rng.nextInt(static_cast<std::int32_t>(g.indices.size())))]);

    std::optional<std::string> tuft;
    // Draw order matters: Java short-circuits, so when tufts is empty the
    // nextDouble() for the tuft test is NEVER drawn. Reproduced exactly —
    // getting this wrong desynchronises every later draw on a shared stream.
    if (!tufts_.empty() && rng.nextDouble() < g.tuftRate) {
        const double o = rng.nextDouble() * tuftCumulative_[tuftCumulative_.size() - 1];
        std::size_t oi = 0;
        while (oi < tuftCumulative_.size() - 1 && o > tuftCumulative_[oi]) {
            oi++;
        }
        tuft = tufts_[oi];
    }
    return Ground{base, tuft};
}

std::string GroundPalette::toString() const {
    std::string sb = std::to_string(groups_.size()) + " base groups, "
                   + std::to_string(tufts_.size()) + " tufts";
    for (const BaseGroup& g : groups_) {
        char label[32];
        std::snprintf(label, sizeof(label), "%-14s", g.label);
        char nums[64];
        std::snprintf(nums, sizeof(nums), "%5.1f%% of ground, %5.1f%% overlaid",
                      g.weight, g.tuftRate * 100);
        sb += "\n      ";
        sb += label;
        sb += nums;
    }
    return sb;
}

} // namespace pzformat
