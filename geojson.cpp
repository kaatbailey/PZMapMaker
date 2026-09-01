#include "geojson.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace pzformat {

GeoJson GeoJson::read(const std::string& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + file);
    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string text = ss.str();
    return parse(text);
}

GeoJson GeoJson::parse(std::string_view text) {
    GeoJson g;
    auto root = Json::parse(text);
    if (!root) return g;
    auto feats = root->get("features");
    if (!feats || !feats->isArray) return g;

    for (const auto& f : feats->array) {
        auto geom = f->get("geometry");
        if (!geom || !geom->isObject) continue;
        auto coords = geom->get("coordinates");
        if (!coords) continue;

        Feature out;
        auto t = geom->get("type");
        // Java: t == null ? "" : t.str — and t.str is null for a non-string,
        // which would NPE downstream. Every real geometry has a string type.
        out.type = (t && t->isStr) ? t->str : "";

        collectRings(coords, out.rings);

        auto props = f->get("properties");
        if (props && props->isObject)
            for (const auto& kv : props->object)
                out.props.emplace_back(kv.first, kv.second->asText());

        if (!out.rings.empty()) g.features.push_back(std::move(out));
    }
    return g;
}

void GeoJson::collectRings(const Json::ValuePtr& node, std::vector<Ring>& out) {
    if (!node || !node->isArray) return;
    if (isPairArray(node)) {
        Ring ring;
        ring.reserve(node->array.size());
        for (const auto& p : node->array)
            ring.push_back({p->array[0]->num, p->array[1]->num});
        out.push_back(std::move(ring));
        return;
    }
    for (const auto& child : node->array) collectRings(child, out);
}

bool GeoJson::isPairArray(const Json::ValuePtr& node) {
    if (!node || !node->isArray || node->array.empty()) return false;
    const auto& first = node->array[0];
    return first->isArray && first->array.size() >= 2 && first->array[0]->isNum &&
           first->array[1]->isNum;
}

}  // namespace pzformat
