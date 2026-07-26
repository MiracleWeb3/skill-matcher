#include "log.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <vector>

namespace sm {
namespace {

namespace fs = std::filesystem;

// Local date, not UTC: the question this answers is "has it got better since Tuesday",
// and Tuesday is whichever one he was sitting here for.
std::string today() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
    if (!localtime_r(&t, &tm)) return "0000-00-00";
    char buf[16];
    std::strftime(buf, sizeof buf, "%Y-%m-%d", &tm);
    return buf;
}

bool is_date(const std::string& s) {
    return s.size() == 10 && s[4] == '-' && s[7] == '-' &&
           std::all_of(s.begin(), s.end(), [](char c) { return c == '-' || (c >= '0' && c <= '9'); });
}

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
    append(today() + "\tsuggest\t" + sanitize(session) + "\t" +
           (decisive ? "decisive" : "choice") + "\t" + sanitize(joined));
}

void log_invocation(const std::string& session, const std::string& skill) {
    if (skill.empty()) return;
    append(today() + "\tinvoke\t" + sanitize(session) + "\t-\t" + sanitize(skill));
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
    std::size_t suggestions = 0, decisive = 0, invocations = 0, undated = 0;
    std::string first, last;
    for (std::string line; std::getline(in, line);) {
        std::istringstream ls(line);
        std::string date, kind, session, mode, payload;
        const bool parsed = static_cast<bool>(std::getline(ls, date, '\t')) &&
                            static_cast<bool>(std::getline(ls, kind, '\t')) &&
                            static_cast<bool>(std::getline(ls, session, '\t')) &&
                            static_cast<bool>(std::getline(ls, mode, '\t')) &&
                            static_cast<bool>(std::getline(ls, payload));
        // Rows written before the date column existed are skipped rather than guessed at:
        // a misread row is worse than a missing one when the whole point is a number. They
        // are one field short, so they fail the parse — count them there, or the tally only
        // ever sees malformed dates and silently reports zero.
        if (!parsed || !is_date(date)) {
            if (!line.empty()) ++undated;
            continue;
        }
        if (first.empty() || date < first) first = date;
        if (date > last) last = date;
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

    // offered/followed per skill, so a matcher that names the wrong thing is visible by name
    // rather than buried in an average.
    std::map<std::string, std::pair<std::size_t, std::size_t>> per_skill;
    std::size_t hit = 0, total = 0;
    for (const auto& [session, names] : offered) {
        const auto it = invoked.find(session);
        for (const auto& n : names) {
            ++total;
            auto& row = per_skill[n];
            ++row.first;
            if (it != invoked.end() && it->second.count(n)) {
                ++hit;
                ++row.second;
            }
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
    if (!first.empty()) std::printf("covering      %s to %s\n", first.c_str(), last.c_str());
    if (undated) std::printf("skipped       %zu row(s) written before dates were logged\n", undated);

    if (!per_skill.empty()) {
        std::vector<std::pair<std::string, std::pair<std::size_t, std::size_t>>> rows(
            per_skill.begin(), per_skill.end());
        std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
            if (a.second.first != b.second.first) return a.second.first > b.second.first;
            return a.first < b.first;
        });
        std::printf("\n%-40s %8s %9s\n", "skill", "offered", "followed");
        for (const auto& [name, counts] : rows) {
            std::printf("%-40s %8zu %9zu\n", name.c_str(), counts.first, counts.second);
        }
    }
    return 0;
}

}  // namespace sm
