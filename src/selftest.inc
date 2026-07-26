// Compiled only under -DSM_SELFTEST, so the hook that runs on every prompt carries no test
// code. No framework: assertions and a count.
//
// The suite builds itself from whatever skills this machine actually has — asking for a skill
// by its own name must return it — so it stays honest on a machine with a different library.
#include <cstdlib>
#include <regex>

namespace {

int checks = 0;

void ok(bool cond, const std::string& what) {
    ++checks;
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what.c_str());
        std::exit(1);
    }
}

int selftest() {
    // Word forms must converge...
    const std::pair<const char*, const char*> pairs[] = {
        {"accessible", "accessibility"}, {"animated", "animation"}, {"animate", "animating"},
        {"optimize", "optimization"},    {"optimizing", "optimized"}, {"configure", "configuration"},
        {"configuring", "configuration"}, {"debugging", "debug"},   {"tests", "testing"},
        {"performance", "performing"}};
    for (const auto& [a, b] : pairs) {
        ok(sm::stem(a) == sm::stem(b), std::string(a) + " and " + b + " must meet");
    }
    // ...without shredding short or technical tokens into collisions.
    for (const char* w : {"vitest", "css", "react", "design", "string", "pinia", "oklch"}) {
        ok(sm::stem(w) == w, std::string("over-stemmed ") + w);
    }

    // Inferred evidence may support what you typed, never outvote it.
    const auto q = sm::expand(sm::tokenize("dashboard"));
    ok(q.at("dashboard") >= 1.0, "a typed word weighs full");
    ok(q.count("chart") && q.at("chart") < 1.0, "an inferred word weighs less");

    const auto skills = sm::build_index();
    ok(!skills.empty(), "no skills indexed - is CLAUDE_CONFIG_DIR right?");

    // Asking for a skill by its own name must surface it.
    int checked = 0;
    for (const auto& s : skills) {
        const std::string slug = s.name.substr(s.name.find(':') + 1);
        std::vector<std::string> words;
        std::string cur;
        for (const char c : slug + "-") {
            if (std::isalnum(static_cast<unsigned char>(c))) { cur.push_back(c); continue; }
            if (!cur.empty() && !sm::is_stopword(cur)) words.push_back(cur);
            cur.clear();
        }
        if (words.size() < 2) continue;  // single-word names are too ambiguous to assert
        std::string query;
        for (const auto& w : words) query += w + " ";
        bool found = false;
        for (const auto& h : sm::match(query, skills)) {
            if (h.name == s.name) { found = true; break; }
        }
        ok(found, s.name + " not found by its own name");
        if (++checked >= 25) break;
    }
    ok(checked > 0, "no multi-word skill names to verify against");

    // Chatter must stay silent - these are the messages you send all day.
    const char* noise[] = {"thanks!", "ok do it", "what time is it", "set that up",
                           "yes continue please", "sounds good, proceed", "looks good to me",
                           "go ahead"};
    for (const char* n : noise) {
        ok(sm::match(n, skills).empty(), std::string("should stay silent on ") + n);
    }

    // The regression that made the whole line ignorable: one incidental word landing on a
    // skill's name ("dashboard chat" - a window, not a chart) used to outrank the topic.
    const std::string report =
        "difference between X chat and what is happening in dashboard chat: it did not read "
        "the conversation, did not pull all messages, and started answering. Another bug. "
        "Overall i see 3 bugs, right? Fix all.";
    const auto hits = sm::match(report, skills);
    int first_debug = -1, first_dashboard = -1;
    for (std::size_t i = 0; i < hits.size(); ++i) {
        std::string desc;
        for (const auto& s : skills) {
            if (s.name == hits[i].name) { desc = s.desc; break; }
        }
        for (auto& c : desc) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const bool is_debug = desc.find("bug") != std::string::npos ||
                              desc.find("debug") != std::string::npos ||
                              desc.find("diagnos") != std::string::npos;
        if (is_debug && first_debug < 0) first_debug = static_cast<int>(i);
        if (hits[i].name.substr(hits[i].name.find(':') + 1) == "dashboard" && first_dashboard < 0) {
            first_dashboard = static_cast<int>(i);
        }
    }
    ok(first_dashboard < 0 || (first_debug >= 0 && first_debug < first_dashboard),
       "incidental name word still outranking the topic");

    // Aliases must have collapsed, or one skill occupies two slots under two names.
    std::vector<std::unordered_set<std::string>> prints;
    for (const auto& s : skills) {
        auto p = sm::fingerprint(s.name.substr(s.name.find(':') + 1), s.desc);
        if (!p.empty()) prints.push_back(std::move(p));
    }
    std::size_t twins = 0;
    for (std::size_t i = 0; i < prints.size(); ++i) {
        for (std::size_t j = i + 1; j < prints.size(); ++j) {
            if (sm::same_skill(prints[i], prints[j])) ++twins;
        }
    }
    ok(twins == 0, "skills indexed twice under different names");

    std::printf("ok - %zu skills indexed, %d name lookups + %zu silences + alias collapse, "
                "%d checks\n",
                skills.size(), checked, sizeof(noise) / sizeof(*noise), checks);
    return 0;
}

}  // namespace
