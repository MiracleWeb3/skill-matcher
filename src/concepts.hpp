// The concept table: bridges the gaps stemming cannot.
//
// Stemming gets you from "animated" to "animation". It cannot get you from "janky" to
// "performance" — different words, not different forms. Each group below is fully connected:
// every member expands to every other, so one line of N words buys N*N bridges.
//
// To extend, drop a word onto the matching line. Word form does not matter, keys are stemmed
// at load. Keep groups conceptually tight: a loose group drags unrelated skills into every
// match, and a suggestion list you learn to skip is the failure this whole tool exists to fix.
//
// GENERATED from the Python original, then frozen — retyping 48 groups by hand is how a port
// silently changes behaviour.
#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace sm {

// stem -> every other stem in any group it belongs to.
const std::unordered_map<std::string, std::unordered_set<std::string>>& synonyms();

// term -> weight. Typed words count 1 + log(times said); inferred ones a flat 0.5, and only
// where a typed word did not already claim the slot.
std::unordered_map<std::string, double> expand(const std::vector<std::string>& terms);

}  // namespace sm
