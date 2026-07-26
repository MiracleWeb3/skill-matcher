#include "text.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <unordered_set>

namespace sm {
namespace {

constexpr std::size_t kMinStem = 4;

// Ordered longest-first, so "ibility" wins before "ity" can nibble at it.
constexpr std::string_view kSuffixes[]{
    "ibility", "ability", "ization", "ational", "uration", "fulness", "ousness", "iveness",
    "izing", "ating", "ative", "ently", "ously", "fully", "ement", "ation", "ition", "uring",
    "ized", "izer", "ated", "ibly", "ness", "ment", "able", "ible", "ance", "ence", "ical",
    "ious", "ing", "ity", "ive", "ize", "ise", "est", "ful", "ous", "ial", "ate", "ure",
    "ed", "er", "ly", "al"};

const std::unordered_set<std::string_view>& stopwords() {
    static const std::unordered_set<std::string_view> s{
        "a","about","above","after","again","against","all","am","an","and","any","are","aren",
        "as","at","be","because","been","before","being","below","between","both","but","by",
        "can","cannot","could","did","didn","do","does","doesn","doing","don","down","during",
        "each","few","for","from","further","had","has","have","having","he","her","here","hers",
        "him","his","how","i","if","in","into","is","isn","it","its","let","me","more","most",
        "my","no","nor","not","of","off","on","once","only","or","other","our","out","over",
        "own","same","she","should","so","some","such","than","that","the","their","them","then",
        "there","these","they","this","those","through","to","too","under","until","up","very",
        "was","we","were","what","when","where","which","while","who","whom","why","will","with",
        "would","you","your","use","using","used","want","wants","need","needs","make","makes",
        "making","get","gets","got","new","via","etc","also","like","just","really","please",
        "help","able","keep","keeps","kept"};
    return s;
}

// Idioms whose words are real skill topics but mean nothing here: "sounds good" must not
// summon the audio skills. A hand scanner, not a regex — the grammar is a fixed phrase list
// with one optional 's', and metal's rule is no regex library.
bool chatter_at(std::string_view s, std::size_t i, std::size_t& len) {
    static constexpr std::string_view kPhrases[]{
        "sounds good", "sound good", "looks good", "look good", "go ahead", "makes sense",
        "make sense", "no worries", "good job", "good work", "good call", "well done",
        "thank you", "nice one"};
    const auto word_edge = [&](std::size_t p) {
        return p == 0 || !(std::isalnum(static_cast<unsigned char>(s[p - 1])));
    };
    if (!word_edge(i)) return false;
    for (const auto& p : kPhrases) {
        if (s.compare(i, p.size(), p) != 0) continue;
        const std::size_t end = i + p.size();
        if (end < s.size() && std::isalnum(static_cast<unsigned char>(s[end]))) continue;
        len = p.size();
        return true;
    }
    return false;
}

}  // namespace

bool is_stopword(std::string_view w) { return stopwords().count(w) > 0; }

std::string stem(std::string w) {
    if (w.size() > 4 && w.size() >= 3 && w.compare(w.size() - 3, 3, "ies") == 0) {
        w = w.substr(0, w.size() - 3) + "y";
    } else if (w.size() > 3 && w.back() == 's' &&
               !(w.size() >= 2 && w.compare(w.size() - 2, 2, "ss") == 0)) {
        w.pop_back();
    }
    for (int pass = 0; pass < 3; ++pass) {
        const std::string before = w;
        for (const auto& suf : kSuffixes) {
            if (w.size() < suf.size()) continue;
            if (w.compare(w.size() - suf.size(), suf.size(), suf) != 0) continue;
            if (w.size() - suf.size() < kMinStem) continue;
            w.resize(w.size() - suf.size());
            // Only right after a strip, or "access"/"class" lose their real double s.
            if (w.size() > kMinStem && w[w.size() - 1] == w[w.size() - 2] &&
                std::string_view("aeiou").find(w[w.size() - 1]) == std::string_view::npos) {
                w.pop_back();  // debugg -> debug
            }
            break;
        }
        if (w == before) break;
    }
    // "decide"/"decided" -> decid, "style"/"styling" -> styl. Without this, e-final verbs
    // never meet their own inflections.
    if (w.size() > kMinStem && w.back() == 'e') w.pop_back();
    return w;
}

std::vector<std::string> tokenize(std::string_view text) {
    std::string low;
    low.reserve(text.size());
    for (const char c : text) low.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

    std::string cleaned;
    cleaned.reserve(low.size());
    for (std::size_t i = 0; i < low.size();) {
        std::size_t len = 0;
        if (chatter_at(low, i, len)) {
            cleaned.push_back(' ');
            i += len;
        } else {
            cleaned.push_back(low[i++]);
        }
    }

    std::vector<std::string> out;
    for (std::size_t i = 0; i < cleaned.size();) {
        if (!std::isalnum(static_cast<unsigned char>(cleaned[i]))) { ++i; continue; }
        const std::size_t start = i;
        while (i < cleaned.size() && std::isalnum(static_cast<unsigned char>(cleaned[i]))) ++i;
        const std::string raw = cleaned.substr(start, i - start);
        if (raw.size() < 2 || is_stopword(raw)) continue;
        out.push_back(stem(raw));
    }
    return out;
}

}  // namespace sm
