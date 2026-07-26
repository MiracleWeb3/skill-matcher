// Scoring a prompt against the index. BM25-ish IDF, with two corrections that took three
// rewrites to find:
//
//   1. Term frequency counts. A word said twice is what the message is about; dropping
//      frequency is what let one passing mention of "dashboard" outrank the actual request.
//   2. Inferred evidence may support what you typed, never outvote it. One word fans out to
//      a dozen concept-group synonyms, and a skill whose description lists all twelve was
//      collecting twelve counts of proof from a single mention.
#pragma once

#include <string>
#include <vector>

#include "index.hpp"

namespace sm {

struct Hit {
    std::string name;
    double score = 0;
    // How many words the user actually typed backed this hit. Margin alone is not confidence:
    // "not sound like ai wrote it" put generating-sounds-with-ai on top by a wide margin, off
    // two incidental words landing on its name. A caller may only speak decisively when the
    // match rests on corroboration, not on one coincidence that happened to win big.
    std::size_t typed = 0;
};

// Ranked, thresholded, capped. Empty when nothing clears the floor — silence is a feature,
// because a hook that fires on every message is one you learn to skip.
std::vector<Hit> match(const std::string& prompt, const std::vector<Skill>& skills);

}  // namespace sm
