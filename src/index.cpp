#include "index.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "json.hpp"
#include "text.hpp"

namespace sm {
namespace {

namespace fs = std::filesystem;

constexpr std::size_t kMinFingerprint = 6;   // tokens needed before two descriptions may be called one skill
constexpr double kAliasSimilarity = 0.85;    // description overlap at which two names are one skill

std::string claude_dir() {
    if (const char* c = std::getenv("CLAUDE_CONFIG_DIR"); c && *c) return c;
    const char* home = std::getenv("HOME");
    return std::string(home ? home : ".") + "/.claude";
}

// Skills that ship inside Claude Code itself. They have no SKILL.md anywhere on disk, so
// scanning cannot see them. A hand-kept list, because there is nothing to read: add a line
// when Claude Code ships a new built-in. A stale entry costs one suggestion, not a crash.
const std::vector<std::pair<std::string, std::string>>& builtins() {
    static const std::vector<std::pair<std::string, std::string>> b{
    {"dataviz",
         "Create charts, graphs, plots, dashboards and data visualizations in any medium: HTML, "
         "SVG, matplotlib, plotly, d3, Recharts. Palettes, axes, legends, tooltips, stat tiles, "
         "heatmaps, sparklines, categorical and diverging colour."},
    {"code-review",
         "Review the current diff for correctness bugs and reuse, simplification and efficiency "
         "cleanups. Can post findings as inline pull request comments or apply them to the working "
         "tree."},
    {"simplify",
         "Review changed code for reuse, simplification, efficiency and altitude cleanups, then "
         "apply the fixes. Quality only, it does not hunt for bugs."},
    {"security-review",
         "Review code for security vulnerabilities, injection, broken auth, leaked secrets and "
         "unsafe patterns."},
    {"run",
         "Launch and drive this project's app to see a change working: start the app, screenshot "
         "it, confirm a change behaves in the real app and not only in tests."},
    {"claude-api",
         "Reference for the Claude API and Anthropic SDK: model ids, pricing, parameters, "
         "streaming, tool use, MCP, agents, prompt caching, token counting, model migration."},
    {"update-config",
         "Configure the Claude Code harness through settings.json: hooks for automated behaviour, "
         "permissions and allowlists, environment variables."},
    {"keybindings-help",
         "Customize keyboard shortcuts, rebind keys and add chord bindings in keybindings.json."},
    {"fewer-permission-prompts",
         "Scan transcripts for common read-only Bash and MCP calls, then write an allowlist into "
         "settings.json to cut permission prompts."},
    {"loop",
         "Run a prompt or slash command on a recurring interval, or let the model pace its own "
         "repeated iterations of a task."},
    {"schedule",
         "Create, update, list or run scheduled cloud agents and routines on a cron schedule, "
         "including one-off future runs."},
    {"artifact-design",
         "Design guidance and fundamentals for Artifacts, the shareable HTML or Markdown pages "
         "published and hosted for other people to view."},
    };
    return b;
}

std::string collapse_ws(std::string_view s) {
    std::string out;
    bool gap = true;
    for (const char c : s) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { gap = true; continue; }
        if (gap && !out.empty()) out.push_back(' ');
        gap = false;
        out.push_back(c);
    }
    return out;
}

void trim_wrapping(std::string& s) {
    constexpr std::string_view kJunk = " >|-\"'";
    while (!s.empty() && kJunk.find(s.front()) != std::string_view::npos) s.erase(s.begin());
    while (!s.empty() && kJunk.find(s.back()) != std::string_view::npos) s.pop_back();
}

// A frontmatter key at column zero: "name:", "allowed-tools:" — what ends a description.
bool key_line_at(std::string_view s, std::size_t i) {
    std::size_t j = i;
    while (j < s.size() && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '_' || s[j] == '-')) ++j;
    return j > i && j < s.size() && s[j] == ':';
}

}  // namespace

std::string read_description(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return "";
    std::string head(8192, '\0');
    in.read(head.data(), static_cast<std::streamsize>(head.size()));
    head.resize(static_cast<std::size_t>(in.gcount()));

    if (head.compare(0, 3, "---") != 0) return "";
    std::size_t p = head.find('\n');
    if (p == std::string::npos) return "";
    // The frontmatter block ends at a line that is exactly "---".
    std::size_t end = std::string::npos;
    for (std::size_t i = p + 1; i + 3 <= head.size();) {
        const std::size_t eol = head.find('\n', i);
        const std::string_view line(head.data() + i, (eol == std::string::npos ? head.size() : eol) - i);
        if (line.substr(0, 3) == "---" && collapse_ws(line) == "---") { end = i; break; }
        if (eol == std::string::npos) break;
        i = eol + 1;
    }
    if (end == std::string::npos) return "";
    const std::string_view fm(head.data() + p + 1, end - p - 1);

    // description: ... running until the next column-zero key, or the end of frontmatter.
    for (std::size_t i = 0; i < fm.size();) {
        const std::size_t eol = fm.find('\n', i);
        const std::size_t stop = (eol == std::string_view::npos ? fm.size() : eol);
        if (fm.compare(i, 12, "description:") == 0) {
            std::size_t from = i + 12;
            std::size_t to = stop;
            for (std::size_t j = stop + 1; j < fm.size();) {
                if (key_line_at(fm, j)) break;
                const std::size_t e2 = fm.find('\n', j);
                to = (e2 == std::string_view::npos ? fm.size() : e2);
                if (e2 == std::string_view::npos) break;
                j = e2 + 1;
            }
            std::string d = collapse_ws(fm.substr(from, to - from));
            trim_wrapping(d);
            return d;
        }
        if (eol == std::string_view::npos) break;
        i = eol + 1;
    }
    return "";
}

std::unordered_set<std::string> fingerprint(const std::string& slug, const std::string& desc) {
    std::unordered_set<std::string> terms;
    for (const auto& t : tokenize(desc)) terms.insert(t);
    for (const auto& t : tokenize(slug)) terms.erase(t);
    if (terms.size() < kMinFingerprint) return {};
    return terms;
}

bool same_skill(const std::unordered_set<std::string>& a, const std::unordered_set<std::string>& b) {
    std::size_t inter = 0;
    for (const auto& x : a) {
        if (b.count(x)) ++inter;
    }
    const std::size_t uni = a.size() + b.size() - inter;
    return uni > 0 && static_cast<double>(inter) >= kAliasSimilarity * static_cast<double>(uni);
}

std::vector<Skill> build_index() {
    const std::string claude = claude_dir();
    const auto settings = json::parse_file(claude + "/settings.json");
    const json::Value* overrides = settings ? settings->get("skillOverrides") : nullptr;

    const auto off = [&](const std::string& slug) {
        if (!overrides) return false;
        const auto* v = overrides->get(slug);
        return v && v->kind == json::Value::Kind::String && v->str == "off";
    };

    std::vector<std::pair<std::string, std::string>> sources{{claude + "/skills", ""}};
    if (const auto installed = json::parse_file(claude + "/plugins/installed_plugins.json")) {
        const auto* plugins = installed->get("plugins");
        const auto* enabled = settings ? settings->get("enabledPlugins") : nullptr;
        if (plugins && enabled) {
            for (const auto& [full, on] : enabled->object) {
                if (!on || !on->truthy()) continue;
                const auto* entries = plugins->get(full);
                if (!entries) continue;
                for (const auto& e : entries->array) {
                    const auto* p = e->get("installPath");
                    if (!p || p->kind != json::Value::Kind::String) continue;
                    sources.emplace_back(p->str + "/skills", full.substr(0, full.find('@')) + ":");
                }
            }
        }
    }

    std::vector<Skill> skills;
    std::unordered_set<std::string> seen;
    std::vector<std::unordered_set<std::string>> seen_content;

    const auto add = [&](const std::string& slug, const std::string& desc, const std::string& prefix) {
        // Same skill vendored twice, by slug or by content; one suggestion is enough.
        if (seen.count(slug)) return;
        const auto print = fingerprint(slug, desc);
        if (!print.empty()) {
            for (const auto& other : seen_content) {
                if (same_skill(print, other)) return;
            }
        }
        seen.insert(slug);
        if (!print.empty()) seen_content.push_back(print);
        Skill s;
        s.name = prefix + slug;
        s.desc = desc;
        for (const auto& t : tokenize(slug)) s.name_terms.insert(t);
        for (const auto& t : tokenize(slug + " " + desc)) s.terms.insert(t);
        skills.push_back(std::move(s));
    };

    for (const auto& [root, prefix] : sources) {
        std::error_code ec;
        if (!fs::is_directory(root, ec)) continue;
        std::vector<std::string> slugs;
        for (const auto& d : fs::directory_iterator(root, ec)) {
            if (d.is_directory(ec) && fs::is_regular_file(d.path() / "SKILL.md", ec)) {
                slugs.push_back(d.path().filename().string());
            }
        }
        std::sort(slugs.begin(), slugs.end());  // stable order, so ties resolve the same way twice
        for (const auto& slug : slugs) {
            if (prefix.empty() && off(slug)) continue;
            const std::string desc = read_description(root + "/" + slug + "/SKILL.md");
            if (!desc.empty()) add(slug, desc, prefix);
        }
    }

    for (const auto& [slug, desc] : builtins()) {
        if (!off(slug)) add(slug, desc, "");
    }
    return skills;
}

}  // namespace sm
