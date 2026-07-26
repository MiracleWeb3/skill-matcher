// Tokenizing: lowercase, drop chatter, drop stopwords, stem what's left.
//
// The stemmer is not trying to be linguistically correct. It only has to land a prompt word
// and a description word on the same string — "accessible" and "accessibility" have to meet
// somewhere, or you get zero matches while owning five accessibility skills.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace sm {

// Suffix stripper, longest-first. Applied to prompts and descriptions alike.
std::string stem(std::string w);

// Lowercase, strip chatter idioms, split on [a-z0-9]+, drop stopwords and 1-char tokens,
// stem the rest. Stopwords are checked on the raw word, before stemming.
std::vector<std::string> tokenize(std::string_view text);

bool is_stopword(std::string_view w);

}  // namespace sm
