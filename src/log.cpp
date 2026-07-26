#include "log.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace sm {
namespace {

namespace fs = std::filesystem;

std::string log_path() {
    const char* home = std::getenv("HOME");
    std::string dir = std::string(home ? home : ".") + "/.claude/skill-matcher";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir + "/events.tsv";
}

// Appends one line. Silent on any failure: instrumentation must never cost a prompt.
void append(const std::string& line) {
    std::ofstream out(log_path(), std::ios::app);
    if (out) out << line << "\n";
}

std::string sanitize(std::string s) {
    for (char& c : s) {
        if (c == '\t' || c == '\n' || c == '\r') c = ' ';
    }
    return s;
}

}  // namespace

void log_suggestion(const std::string& session, const std::vector<std::string>& names,
                    bool decisive) {
    if (names.empty()) return;
    std::string joined;
    for (const auto& n : names) {
        if (!joined.empty()) joined += ",";
        joined += n;
    }
    append("suggest\t" + sanitize(session) + "\t" + (decisive ? "decisive" : "choice") + "\t" +
           sanitize(joined));
}

void log_invocation(const std::string& session, const std::string& skill) {
    if (skill.empty()) return;
    append("invoke\t" + sanitize(session) + "\t-\t" + sanitize(skill));
}

int print_stats() {
    std::ifstream in(log_path());
    if (!in) {
        std::printf("no log yet at %s — the hook has not run\n", log_path().c_str());
        return 0;
    }
    // Offered per session, and invoked per session. A suggestion counts as followed when that
    // skill is invoked anywhere later in the same session: the model may reasonably act on it
    // a turn or two after it was offered.
    std::map<std::string, std::set<std::string>> offered, invoked;
    std::size_t suggestions = 0, decisive = 0, invocations = 0;
    for (std::string line; std::getline(in, line);) {
        std::istringstream ls(line);
        std::string kind, session, mode, payload;
        if (!std::getline(ls, kind, '\t') || !std::getline(ls, session, '\t') ||
            !std::getline(ls, mode, '\t') || !std::getline(ls, payload)) {
            continue;
        }
        if (kind == "suggest") {
            ++suggestions;
            if (mode == "decisive") ++decisive;
            std::istringstream ps(payload);
            for (std::string n; std::getline(ps, n, ',');) offered[session].insert(n);
        } else if (kind == "invoke") {
            ++invocations;
            invoked[session].insert(payload);
        }
    }

    std::size_t hit = 0, total = 0;
    for (const auto& [session, names] : offered) {
        const auto it = invoked.find(session);
        for (const auto& n : names) {
            ++total;
            if (it != invoked.end() && it->second.count(n)) ++hit;
        }
    }
    std::printf("suggestions   %zu  (%zu decisive, %zu offered a choice)\n", suggestions, decisive,
                suggestions - decisive);
    std::printf("skills named  %zu distinct, across %zu sessions\n", total, offered.size());
    std::printf("invocations   %zu logged\n", invocations);
    if (total) {
        std::printf("followed      %zu of %zu  (%.0f%%)\n", hit, total,
                    100.0 * static_cast<double>(hit) / static_cast<double>(total));
    } else {
        std::printf("followed      no suggestions logged yet\n");
    }
    return 0;
}

}  // namespace sm
