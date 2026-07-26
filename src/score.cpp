#include "score.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "concepts.hpp"
#include "text.hpp"

namespace sm {
namespace {

// Leans toward recall — a wrong suggestion costs one glance, a missing one costs a skill you
// never use — but not so far that the list becomes wallpaper.
constexpr std::size_t kMaxCandidates = 6;
constexpr double kMinScore = 4.0;        // floor when qualifying on multiple typed terms
constexpr double kMinNameScore = 3.0;    // floor when the prompt hits the skill's own name
constexpr double kRelCutoff = 0.30;      // drop hits weaker than this fraction of the best
constexpr double kNameWeight = 2.0;      // a term in the skill's own name beats one in its prose
constexpr double kMinSingleTerm = 8.0;   // floor when one typed word is the entire case
constexpr double kNameCoverage = 0.34;   // share of typed terms a lone name hit must carry

}  // namespace

std::vector<Hit> match(const std::string& prompt, const std::vector<Skill>& skills) {
    const double n = static_cast<double>(skills.size());
    if (skills.empty()) return {};

    std::unordered_map<std::string, double> df;
    for (const auto& s : skills) {
        for (const auto& t : s.terms) df[t] += 1.0;
    }

    const auto query = expand(tokenize(prompt));
    std::vector<Hit> scored;
    for (const auto& s : skills) {
        std::vector<std::string> hits;
        for (const auto& [t, _] : query) {
            if (s.terms.count(t)) hits.push_back(t);
        }
        if (hits.empty()) continue;

        std::size_t typed_hits = 0, name_typed = 0, typed_total = 0;
        for (const auto& [t, w] : query) {
            if (w >= 1.0) ++typed_total;
        }
        bool any_name = false;
        for (const auto& t : hits) {
            const bool is_typed = query.at(t) >= 1.0;
            if (is_typed) ++typed_hits;
            if (s.name_terms.count(t)) {
                any_name = true;
                if (is_typed) ++name_typed;
            }
        }
        if (typed_hits == 0) continue;  // a match built purely from synonyms is the table talking to itself

        // A name hit is strong evidence only when the prompt is actually ABOUT that word.
        // "dashboard" inside a twenty-word bug report is incidental. Corroboration has to come
        // from words you typed: counting inferred ones lets a single word vouch for itself.
        const bool strong_name =
            any_name && (typed_hits >= 2 ||
                         static_cast<double>(name_typed) >= static_cast<double>(typed_total) * kNameCoverage);

        double said = 0, inferred = 0;
        for (const auto& t : hits) {
            const double d = df[t];
            const double idf = std::log((n - d + 0.5) / (d + 0.5) + 1.0);
            const double weight = (strong_name && s.name_terms.count(t)) ? kNameWeight : 1.0;
            const double q = query.at(t);
            (q >= 1.0 ? said : inferred) += idf * weight * q;
        }
        const double score = said + std::min(inferred, said);

        double floor = kMinScore;
        if (strong_name) floor = kMinNameScore;
        else if (typed_hits == 1) floor = kMinSingleTerm;
        if (score >= floor) scored.push_back({s.name, score, typed_hits});
    }

    // Tie on score goes to the shorter name: typing "ponytail" wants the skill itself, not
    // ponytail-review, and sorting on the pair alone buried the parent under its own children.
    std::sort(scored.begin(), scored.end(), [](const Hit& a, const Hit& b) {
        if (a.score != b.score) return a.score > b.score;
        if (a.name.size() != b.name.size()) return a.name.size() < b.name.size();
        return a.name < b.name;
    });
    if (scored.size() > kMaxCandidates) scored.resize(kMaxCandidates);
    scored.erase(std::remove_if(scored.begin(), scored.end(),
                                [](const Hit& h) { return h.score < kMinScore; }),
                 scored.end());
    if (scored.empty()) return {};
    const double best = scored.front().score;
    scored.erase(std::remove_if(scored.begin(), scored.end(),
                                [&](const Hit& h) { return h.score < best * kRelCutoff; }),
                 scored.end());
    return scored;
}

}  // namespace sm
