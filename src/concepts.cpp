#include "concepts.hpp"

#include <cmath>

#include "text.hpp"

namespace sm {
namespace {

// 48 groups, 407 words. See the header: generated, then frozen.
const std::vector<std::vector<std::string>>& groups() {
    static const std::vector<std::vector<std::string>> g{
    {"slow", "sluggish", "janky", "jank", "laggy", "lag", "stutter", "performance", "optimize", "speed", "fps", "framerate", "bottleneck", "profiling", "faster"},
    {"a11y", "accessibility", "wcag", "aria", "screenreader", "keyboard", "contrast"},
    {"bug", "buggy", "broken", "crash", "error", "exception", "traceback", "stacktrace", "failing", "debug", "diagnose", "troubleshoot", "regression", "repro", "fix"},
    {"test", "spec", "unit", "integration", "e2e", "coverage", "mock", "assertion", "flaky", "vitest", "jest", "playwright", "tdd"},
    {"design", "ui", "ux", "visual", "aesthetic", "pretty", "ugly", "beautiful", "polish", "styling", "taste", "slop", "generic", "templated", "redesign"},
    {"animation", "animated", "motion", "transition", "easing", "spring", "tween", "keyframe", "stagger", "microinteraction", "smooth", "smoothly", "snappy", "bounce", "fade", "reveal", "collapse", "expand"},
    {"color", "colour", "palette", "hue", "saturation", "oklch", "hex", "rgb", "hsl", "theme", "gradient"},
    {"typography", "font", "typeface", "kerning", "leading", "typescale"},
    {"layout", "spacing", "padding", "margin", "grid", "flexbox", "alignment", "whitespace", "gap", "bento"},
    {"responsive", "mobile", "breakpoint", "viewport", "tablet", "desktop"},
    {"state", "store", "redux", "zustand", "pinia", "vuex", "signal", "reactivity"},
    {"routing", "router", "route", "navigation", "redirect", "breadcrumb"},
    {"build", "bundler", "bundle", "webpack", "vite", "rollup", "rolldown", "esbuild", "tsdown", "compile", "transpile"},
    {"npm", "yarn", "pnpm", "package", "dependency", "install", "lockfile", "workspace", "monorepo", "turborepo"},
    {"react", "jsx", "hook", "nextjs", "next"},
    {"vue", "nuxt", "composable", "sfc", "vueuse"},
    {"svelte", "sveltekit", "rune"},
    {"3d", "threejs", "webgl", "shader", "glsl", "mesh", "geometry", "texture", "material", "scene", "raycasting"},
    {"audio", "sound", "webaudio", "synthesis", "waveform"},
    {"video", "remotion", "lottie", "bodymovin", "footage"},
    {"browser", "playwright", "puppeteer", "scrape", "crawl", "screenshot", "automation", "headless", "chrome", "devtools", "cdp"},
    {"git", "commit", "branch", "merge", "rebase", "worktree", "changelog"},
    {"docs", "documentation", "readme", "jsdoc", "tsdoc", "docstring"},
    {"security", "auth", "authentication", "authorization", "oauth", "jwt", "secret", "vulnerability", "xss", "csrf", "injection", "sanitize"},
    {"db", "database", "sql", "schema", "migration", "orm", "postgres", "sqlite"},
    {"api", "endpoint", "rest", "graphql", "http", "fetch", "server", "backend"},
    {"refactor", "cleanup", "simplify", "deduplicate", "unused", "bloat", "overengineered", "debt", "boilerplate", "yagni"},
    {"architecture", "modular", "abstraction", "coupling", "seam", "boundary", "domain"},
    {"remember", "recall", "memory", "history", "previous", "earlier", "session", "transcript", "wiki", "decided", "decision", "discussed", "agreed", "chose", "yesterday", "conversation"},
    {"chart", "graph", "plot", "dataviz", "visualization", "dashboard", "metric", "kpi", "sparkline", "heatmap", "legend", "axis", "tooltip"},
    {"slide", "presentation", "deck", "talk", "keynote", "powerpoint", "pptx", "slidev"},
    {"seo", "metadata", "opengraph", "canonical", "sitemap", "robots", "favicon"},
    {"form", "input", "validation", "field", "checkbox", "submit", "placeholder"},
    {"image", "svg", "icon", "asset", "sprite", "thumbnail"},
    {"i18n", "internationalization", "localization", "l10n", "translation", "locale"},
    {"deploy", "release", "publish", "ci", "pipeline", "vercel", "netlify", "docker"},
    {"cli", "terminal", "shell", "bash", "tmux", "stdout"},
    {"ios", "android", "swiftui", "reactnative", "flutter", "native"},
    {"plan", "brainstorm", "requirements", "roadmap", "prd", "ticket", "backlog"},
    {"review", "audit", "critique", "feedback", "lint", "eslint", "prettier", "quality"},
    {"component", "widget", "primitive", "button", "modal", "dropdown", "dialog", "tooltip", "popover", "navbar", "sidebar", "accordion", "toast", "shadcn"},
    {"landing", "marketing", "hero", "cta", "pricing", "portfolio", "website"},
    {"css", "tailwind", "unocss", "stylesheet", "daisyui"},
    {"js", "javascript", "ts", "typescript", "node", "bun", "deno"},
    {"memoryleak", "leak", "heap", "garbage", "oom"},
    {"loading", "skeleton", "spinner", "placeholder", "suspense", "streaming"},
    {"empty", "onboarding", "errorstate", "edgecase", "fallback"},
    {"cache", "caching", "memoize", "invalidation", "revalidate", "stale"},
    };
    return g;
}

constexpr double kSynonymWeight = 0.5;  // an inferred term is weaker evidence than a typed one

}  // namespace

const std::unordered_map<std::string, std::unordered_set<std::string>>& synonyms() {
    static const auto table = [] {
        std::unordered_map<std::string, std::unordered_set<std::string>> t;
        for (const auto& group : groups()) {
            std::vector<std::string> stems;
            stems.reserve(group.size());
            for (const auto& w : group) stems.push_back(stem(w));
            for (std::size_t i = 0; i < stems.size(); ++i) {
                for (std::size_t j = 0; j < stems.size(); ++j) {
                    if (i != j) t[stems[i]].insert(stems[j]);
                }
            }
        }
        return t;
    }();
    return table;
}

std::unordered_map<std::string, double> expand(const std::vector<std::string>& terms) {
    // Repetition counts. A prompt saying "bug" twice and "fix" once is about bugs, even when
    // some rarer word wandered through it once. Sublinear: saying it three times is not three
    // times the request.
    std::unordered_map<std::string, double> weights;
    for (const auto& t : terms) weights[t] += 1.0;
    for (auto& [t, w] : weights) w = 1.0 + std::log(w);

    // Inferred terms never overwrite a typed one — setdefault semantics, deliberately.
    std::vector<std::string> typed;
    typed.reserve(weights.size());
    for (const auto& [t, _] : weights) typed.push_back(t);
    for (const auto& t : typed) {
        const auto it = synonyms().find(t);
        if (it == synonyms().end()) continue;
        for (const auto& syn : it->second) weights.emplace(syn, kSynonymWeight);
    }
    return weights;
}

}  // namespace sm
