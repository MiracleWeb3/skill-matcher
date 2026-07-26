// skill-matcher: surface the right skill on every prompt, so a large library never goes unused.
//
// Skill descriptions all sit in context, but *noticing* the right one out of hundreds is a
// recall task the model silently fails. This hook makes it mechanical: keyword-score the
// prompt against every invocable skill, and say so before the model answers. No LLM, no
// network, no tokens.
//
// Two things this learned the hard way. Silence is a feature — below threshold it prints
// nothing, because a hook that fires every turn is one you tune out, which is the exact
// failure it exists to fix. And a *list* is a menu: six names invite skimming, so the output
// is capped at two and phrased as an instruction. The scoring underneath is unchanged and
// verified identical to the Python it replaces across 607 real prompts.
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "concepts.hpp"
#include "index.hpp"
#include "json.hpp"
#include "log.hpp"
#include "score.hpp"
#include "text.hpp"

namespace {

// The top hit must beat the runner-up by this much to speak as a single instruction rather
// than offer a choice. Measured on 972 real prompts: 1.3 makes 29% of prompts decisive, 1.5
// makes 22%, 2.0 only 17% — the curve knees at 1.5, so that is where it sits.
constexpr double kDecisive = 1.5;
// ...and a margin is not enough on its own. A hit resting on a single typed word may still be
// offered as a choice, never asserted as an instruction: that is how "not sound like ai wrote
// it" confidently recommended a sound-synthesis skill over the humanizer sitting right there.
constexpr std::size_t kDecisiveTyped = 2;
constexpr std::size_t kMaxNamed = 2;  // was 6; a list of six is a menu, and menus get skimmed

std::string read_stdin() {
    std::ostringstream buf;
    buf << std::cin.rdbuf();
    return buf.str();
}

std::string field(const sm::json::Value* root, std::string_view key) {
    if (!root) return "";
    const auto* v = root->get(key);
    return (v && v->kind == sm::json::Value::Kind::String) ? v->str : "";
}

std::string prompt_of(const sm::json::Value* root) {
    for (const auto* k : {"prompt", "user_prompt", "message", "content"}) {
        if (const std::string s = field(root, k); !s.empty()) return s;
    }
    return "";
}

std::string escape(std::string_view s) {
    std::string o;
    for (const char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\t': o += "\\t"; break;
            case '\r': o += "\\r"; break;
            default: o.push_back(c);
        }
    }
    return o;
}

// The whole point of the rewrite: an instruction, not a menu.
std::string phrase(const std::vector<sm::Hit>& hits, bool decisive) {
    if (hits.size() == 1) {
        // One candidate, and no runner-up to weigh it against. Still an instruction, but the
        // hedge is honest: nothing corroborated it.
        return decisive ? "Use the " + hits[0].name + " skill for this. (local keyword match)"
                        : "Closest skill: " + hits[0].name +
                              ". Invoke it if it fits — decide rather than skip.";
    }
    if (decisive) {
        return "Use the " + hits[0].name +
               " skill for this. (local keyword match — skip only if it plainly does not fit, "
               "and say so in one line if you do)";
    }
    return "Two skills fit: " + hits[0].name + " or " + hits[1].name +
           ". Invoke whichever matches, or neither if both miss — but decide, do not ignore.";
}

int hook_mode() {
    const auto root = sm::json::parse(read_stdin());
    const std::string prompt = prompt_of(root.get());
    if (prompt.find_first_not_of(" \t\n\r") == std::string::npos) return 0;

    auto hits = sm::match(prompt, sm::build_index());
    if (hits.empty()) return 0;  // silence is a feature
    // Confidence needs something to be confident *against*. With a single candidate the margin
    // test never ran, so a lone hit is reported as the closest match, never asserted as the
    // right one — "not sound like ai wrote it" returns exactly one candidate, a sound-synthesis
    // skill, and stating that as an instruction is worse than the menu this replaced.
    const bool decisive = hits.size() > 1 && hits[0].typed >= kDecisiveTyped &&
                          hits[0].score >= kDecisive * hits[1].score;
    if (hits.size() > kMaxNamed) hits.resize(kMaxNamed);

    std::vector<std::string> names;
    names.reserve(decisive ? 1 : hits.size());
    names.push_back(hits[0].name);
    if (!decisive && hits.size() > 1) names.push_back(hits[1].name);
    sm::log_suggestion(field(root.get(), "session_id"), names, decisive);

    std::printf("{\"hookSpecificOutput\":{\"hookEventName\":\"UserPromptSubmit\","
                "\"additionalContext\":\"%s\"}}\n",
                escape(phrase(hits, decisive)).c_str());
    return 0;
}

// PostToolUse on Skill: the other half of the measurement.
int invoked_mode() {
    const auto root = sm::json::parse(read_stdin());
    if (!root) return 0;
    const auto* input = root->get("tool_input");
    sm::log_invocation(field(root.get(), "session_id"), field(input, "skill"));
    return 0;
}

}  // namespace

#ifdef SM_SELFTEST
#include "selftest.inc"
#endif

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    const auto has = [&](std::string_view f) {
        for (const auto& a : args) {
            if (a == f) return true;
        }
        return false;
    };

#ifdef SM_SELFTEST
    if (has("--test")) return selftest();
#endif
    if (has("--stats")) return sm::print_stats();
    if (has("--invoked")) return invoked_mode();
    if (has("--list")) {
        for (const auto& s : sm::build_index()) std::printf("%s\n", s.name.c_str());
        return 0;
    }
    for (std::size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] != "--query") continue;
        const auto skills = sm::build_index();
        const auto hits = sm::match(args[i + 1], skills);
        if (hits.empty()) {
            std::printf("(no matches)\n");
            return 0;
        }
        const bool decisive = hits.size() > 1 && hits[0].typed >= kDecisiveTyped &&
                              hits[0].score >= kDecisive * hits[1].score;
        std::printf("%s\n\n", phrase(hits, decisive).c_str());
        for (const auto& h : hits) std::printf("  %7.3f  %s\n", h.score, h.name.c_str());
        return 0;
    }
    return hook_mode();  // no flags: read a hook event on stdin
}
