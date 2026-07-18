#!/usr/bin/env python3
"""Surface relevant skills on every prompt, so a large skill library never goes unused.

The problem this solves: skill descriptions all sit in context, but *noticing* the right
one out of hundreds is a recall task the model silently fails. This hook makes it mechanical --
BM25-ish keyword scoring of the prompt against every enabled skill's name+description,
top matches injected as context before the model answers. No LLM, no network, no tokens.

Silence is a feature: below threshold it prints nothing. A hook that fires noisily every
turn gets tuned out, which is the same failure it was built to fix.

Usage:
    skill-matcher.py                 # hook mode: reads hook JSON on stdin
    skill-matcher.py --query "text"  # probe scoring by hand
    skill-matcher.py --list          # what got indexed
    skill-matcher.py --test          # self-check
"""
import glob
import json
import math
import os
import re
import sys

CLAUDE = os.environ.get("CLAUDE_CONFIG_DIR") or os.path.expanduser("~/.claude")
# Leans slightly toward recall -- a wrong suggestion costs one glance, a missing one costs a
# skill you never use -- but not so far that the list becomes wallpaper and gets ignored.
MAX_HITS = 6
MIN_SCORE = 4.0          # floor when qualifying on multiple typed terms
MIN_NAME_SCORE = 3.0     # floor when the prompt hits the skill's own name
REL_CUTOFF = 0.30        # drop hits weaker than this fraction of the best hit
NAME_WEIGHT = 2.0        # a term in the skill's own name beats one in its prose
SYNONYM_WEIGHT = 0.5     # an inferred term is weaker evidence than a typed one

STOP = set("""a about above after again against all am an and any are aren as at be because been
before being below between both but by can cannot could did didn do does doesn doing don down
during each few for from further had has have having he her here hers him his how i if in into is
isn it its let me more most my no nor not of off on once only or other our out over own same she
should so some such than that the their them then there these they this those through to too under
until up very was we were what when where which while who whom why will with would you your
use using used want wants need needs make makes making get gets got new via etc also like just
really please help able keep keeps kept""".split())

# Bridges the gaps stemming can't: different words, not different forms of one word
# (you say "janky", the skill says "performance"). Written as concept groups -- every
# member expands to every other member, so one line buys N*N bridges. To extend, drop a
# word onto the matching line; word form doesn't matter, keys get stemmed at load.
# Keep groups conceptually tight: a loose group drags unrelated skills into every match.
CONCEPTS = [
    ["slow", "sluggish", "janky", "jank", "laggy", "lag", "stutter", "performance",
     "optimize", "speed", "fps", "framerate", "bottleneck", "profiling", "faster"],
    ["a11y", "accessibility", "wcag", "aria", "screenreader", "keyboard", "contrast"],
    ["bug", "buggy", "broken", "crash", "error", "exception", "traceback", "stacktrace",
     "failing", "debug", "diagnose", "troubleshoot", "regression", "repro", "fix"],
    ["test", "spec", "unit", "integration", "e2e", "coverage", "mock", "assertion",
     "flaky", "vitest", "jest", "playwright", "tdd"],
    ["design", "ui", "ux", "visual", "aesthetic", "pretty", "ugly", "beautiful", "polish",
     "styling", "taste", "slop", "generic", "templated", "redesign"],
    ["animation", "animated", "motion", "transition", "easing", "spring", "tween",
     "keyframe", "stagger", "microinteraction", "smooth", "smoothly", "snappy",
     "bounce", "fade", "reveal", "collapse", "expand"],
    ["color", "colour", "palette", "hue", "saturation", "oklch", "hex", "rgb", "hsl",
     "theme", "gradient"],
    ["typography", "font", "typeface", "kerning", "leading", "typescale"],
    ["layout", "spacing", "padding", "margin", "grid", "flexbox", "alignment",
     "whitespace", "gap", "bento"],
    ["responsive", "mobile", "breakpoint", "viewport", "tablet", "desktop"],
    ["state", "store", "redux", "zustand", "pinia", "vuex", "signal", "reactivity"],
    ["routing", "router", "route", "navigation", "redirect", "breadcrumb"],
    ["build", "bundler", "bundle", "webpack", "vite", "rollup", "rolldown", "esbuild",
     "tsdown", "compile", "transpile"],
    ["npm", "yarn", "pnpm", "package", "dependency", "install", "lockfile", "workspace",
     "monorepo", "turborepo"],
    ["react", "jsx", "hook", "nextjs", "next"],
    ["vue", "nuxt", "composable", "sfc", "vueuse"],
    ["svelte", "sveltekit", "rune"],
    ["3d", "threejs", "webgl", "shader", "glsl", "mesh", "geometry", "texture",
     "material", "scene", "raycasting"],
    ["audio", "sound", "webaudio", "synthesis", "waveform"],
    ["video", "remotion", "lottie", "bodymovin", "footage"],
    ["browser", "playwright", "puppeteer", "scrape", "crawl", "screenshot", "automation",
     "headless", "chrome", "devtools", "cdp"],
    ["git", "commit", "branch", "merge", "rebase", "worktree", "changelog"],
    ["docs", "documentation", "readme", "jsdoc", "tsdoc", "docstring"],
    ["security", "auth", "authentication", "authorization", "oauth", "jwt", "secret",
     "vulnerability", "xss", "csrf", "injection", "sanitize"],
    ["db", "database", "sql", "schema", "migration", "orm", "postgres", "sqlite"],
    ["api", "endpoint", "rest", "graphql", "http", "fetch", "server", "backend"],
    ["refactor", "cleanup", "simplify", "deduplicate", "unused", "bloat",
     "overengineered", "debt", "boilerplate", "yagni"],
    ["architecture", "modular", "abstraction", "coupling", "seam", "boundary", "domain"],
    ["remember", "recall", "memory", "history", "previous", "earlier", "session",
     "transcript", "wiki", "decided", "decision", "discussed", "agreed", "chose",
     "yesterday", "conversation"],
    ["chart", "graph", "plot", "dataviz", "visualization", "dashboard", "metric", "kpi",
     "sparkline", "heatmap", "legend", "axis", "tooltip"],
    ["slide", "presentation", "deck", "talk", "keynote", "powerpoint", "pptx", "slidev"],
    ["seo", "metadata", "opengraph", "canonical", "sitemap", "robots", "favicon"],
    ["form", "input", "validation", "field", "checkbox", "submit", "placeholder"],
    ["image", "svg", "icon", "asset", "sprite", "thumbnail"],
    ["i18n", "internationalization", "localization", "l10n", "translation", "locale"],
    ["deploy", "release", "publish", "ci", "pipeline", "vercel", "netlify", "docker"],
    ["cli", "terminal", "shell", "bash", "tmux", "stdout"],
    ["ios", "android", "swiftui", "reactnative", "flutter", "native"],
    ["plan", "brainstorm", "requirements", "roadmap", "prd", "ticket", "backlog"],
    ["review", "audit", "critique", "feedback", "lint", "eslint", "prettier", "quality"],
    ["component", "widget", "primitive", "button", "modal", "dropdown", "dialog",
     "tooltip", "popover", "navbar", "sidebar", "accordion", "toast", "shadcn"],
    ["landing", "marketing", "hero", "cta", "pricing", "portfolio", "website"],
    ["css", "tailwind", "unocss", "stylesheet", "daisyui"],
    ["js", "javascript", "ts", "typescript", "node", "bun", "deno"],
    ["memoryleak", "leak", "heap", "garbage", "oom"],
    ["loading", "skeleton", "spinner", "placeholder", "suspense", "streaming"],
    ["empty", "onboarding", "errorstate", "edgecase", "fallback"],
    ["cache", "caching", "memoize", "invalidation", "revalidate", "stale"],
]


# Ordered longest-first. Applied to descriptions AND prompts, so both collapse to the same
# root: "accessible"/"accessibility" -> access, "animated"/"animation" -> anim. Linguistic
# correctness doesn't matter here, only that both sides land on the same string.
SUFFIXES = (
    "ibility", "ability", "ization", "ational", "uration", "fulness", "ousness", "iveness",
    "izing", "ating", "ative", "ently", "ously", "fully", "ement", "ation", "ition", "uring",
    "ized", "izer", "ated", "ibly", "ness", "ment", "able", "ible", "ance", "ence", "ical",
    "ious", "ing", "ity", "ive", "ize", "ise", "est", "ful", "ous", "ial", "ate", "ure",
    "ed", "er", "ly", "al",
)
MIN_STEM = 4


def stem(w):
    if len(w) > 4 and w.endswith("ies"):
        w = w[:-3] + "y"
    elif len(w) > 3 and w.endswith("s") and not w.endswith("ss"):
        w = w[:-1]
    for _ in range(3):
        before = w
        for suf in SUFFIXES:
            if w.endswith(suf) and len(w) - len(suf) >= MIN_STEM:
                w = w[:-len(suf)]
                # only right after a strip, or "access"/"class" lose their real double s
                if len(w) > MIN_STEM and w[-1] == w[-2] and w[-1] not in "aeiou":
                    w = w[:-1]  # debugg -> debug
                break
        if w == before:
            break
    # "decide"/"decided" -> decid, "style"/"styling" -> styl. Without this, e-final verbs
    # never meet their own inflections.
    if len(w) > MIN_STEM and w.endswith("e"):
        w = w[:-1]
    return w


# Idioms whose words are real skill topics but mean nothing here -- "sounds good" must
# not summon the audio skills. Stripped before tokenizing.
CHATTER = re.compile(r"\b(sounds? good|looks? good|go ahead|makes? sense|no worries|"
                     r"good (job|work|call)|well done|thank you|nice one)\b")


def tokenize(text):
    return [stem(w) for w in re.findall(r"[a-z0-9]+", CHATTER.sub(" ", text.lower()))
            if w not in STOP and len(w) >= 2]


def _build_synonyms():
    table = {}
    for group in CONCEPTS:
        stems = [stem(w) for w in group]
        for i, s in enumerate(stems):
            table.setdefault(s, set()).update(x for j, x in enumerate(stems) if j != i)
    return table


SYNONYMS = _build_synonyms()


def expand(terms):
    """Query-side only: adding synonyms to docs would blur every skill together.
    Returns term -> weight, since a word you actually typed is stronger evidence than
    one a concept group inferred for you."""
    weights = {t: 1.0 for t in terms}
    for t in terms:
        for syn in SYNONYMS.get(t, ()):
            weights.setdefault(syn, SYNONYM_WEIGHT)
    return weights


def read_description(path):
    try:
        with open(path, encoding="utf-8", errors="ignore") as fh:
            head = fh.read(8192)
    except OSError:
        return ""
    m = re.match(r"^---\s*\n(.*?)\n---", head, re.S)
    if not m:
        return ""
    d = re.search(r"^description:\s*(.*?)(?=\n[a-zA-Z_-]+:|\Z)", m.group(1), re.S | re.M)
    return re.sub(r"\s+", " ", d.group(1)).strip(" >|-\"'") if d else ""


def build_index():
    """Every skill the model can actually invoke right now: user skills that aren't
    overridden off, plus skills from enabled plugins."""
    try:
        settings = json.load(open(os.path.join(CLAUDE, "settings.json")))
    except (OSError, ValueError):
        settings = {}
    overrides = settings.get("skillOverrides", {})

    sources = [(os.path.join(CLAUDE, "skills"), "")]
    try:
        installed = json.load(open(os.path.join(CLAUDE, "plugins/installed_plugins.json")))["plugins"]
        for full, on in settings.get("enabledPlugins", {}).items():
            if not on:
                continue
            for entry in installed.get(full, []):
                sources.append((os.path.join(entry["installPath"], "skills"), full.split("@")[0] + ":"))
    except (OSError, ValueError, KeyError):
        pass

    skills, seen = [], set()
    for root, prefix in sources:
        for path in sorted(glob.glob(os.path.join(root, "*", "SKILL.md"))):
            slug = os.path.basename(os.path.dirname(path))
            if not prefix and overrides.get(slug) == "off":
                continue
            if slug in seen:      # same skill vendored twice; one suggestion is enough
                continue
            desc = read_description(path)
            if not desc:
                continue
            seen.add(slug)
            skills.append({
                "name": prefix + slug,
                "desc": desc,
                "name_terms": set(tokenize(slug)),
                "terms": set(tokenize(slug + " " + desc)),
            })
    return skills


def match(prompt, skills):
    n = len(skills)
    if not n:
        return []
    df = {}
    for s in skills:
        for t in s["terms"]:
            df[t] = df.get(t, 0) + 1

    query = expand(tokenize(prompt))
    scored = []
    for s in skills:
        hits = set(query) & s["terms"]
        if not hits:
            continue
        score = 0.0
        for t in hits:
            idf = math.log((n - df[t] + 0.5) / (df[t] + 0.5) + 1.0)
            score += idf * (NAME_WEIGHT if t in s["name_terms"] else 1.0) * query[t]
        # Either you hit the skill's own name, or you hit several things you actually
        # typed. One generic word in a description ("time", "set") is not a signal.
        if hits & s["name_terms"]:
            floor = MIN_NAME_SCORE
        elif sum(1 for t in hits if query[t] == 1.0) >= 2:
            floor = MIN_SCORE
        else:
            continue
        if score >= floor:
            scored.append((score, s["name"]))

    scored.sort(reverse=True)
    scored = [x for x in scored[:MAX_HITS] if x[0] >= MIN_SCORE]
    return [name for score, name in scored if score >= scored[0][0] * REL_CUTOFF]


def extract_prompt(payload):
    if isinstance(payload, dict):
        for key in ("prompt", "user_prompt", "message", "content"):
            if isinstance(payload.get(key), str):
                return payload[key]
    return ""


def self_test():
    # word forms must converge...
    for a, b in [("accessible", "accessibility"), ("animated", "animation"),
                 ("animate", "animating"), ("optimize", "optimization"),
                 ("optimizing", "optimized"), ("configure", "configuration"),
                 ("configuring", "configuration"), ("debugging", "debug"),
                 ("tests", "testing"), ("performance", "performing")]:
        assert stem(a) == stem(b), f"{a}->{stem(a)} != {b}->{stem(b)}"
    # ...without shredding short/technical tokens into collisions
    for w in ("vitest", "css", "react", "design", "string", "pinia", "oklch"):
        assert stem(w) == w, f"over-stemmed {w} -> {stem(w)}"

    skills = build_index()
    assert skills, "no skills indexed -- is CLAUDE_CONFIG_DIR right?"

    # Derived from whatever skills this machine actually has, so the suite is portable:
    # asking for a skill by its own name must surface that skill.
    checked = 0
    for s in skills:
        words = [w for w in re.findall(r"[a-z0-9]+", s["name"].split(":")[-1]) if w not in STOP]
        if len(words) < 2:
            continue                        # single-word names are too ambiguous to assert
        hits = match(" ".join(words), skills)
        assert s["name"] in hits, f"{s['name']} not found by its own name -> {hits}"
        checked += 1
        if checked >= 25:
            break
    assert checked, "no multi-word skill names to verify against"

    # Chatter must stay silent -- these are the messages you send all day.
    noise = ("thanks!", "ok do it", "what time is it", "set that up", "yes continue please",
             "sounds good, proceed", "looks good to me", "go ahead")
    for n_ in noise:
        assert not match(n_, skills), f"should stay silent on {n_!r}: {match(n_, skills)}"

    print(f"ok - {len(skills)} skills indexed, {checked} name lookups + "
          f"{len(noise)} silences pass")


def main():
    if "--test" in sys.argv:
        return self_test()
    if "--list" in sys.argv:
        for s in build_index():
            print(s["name"])
        return
    if "--query" in sys.argv:
        text = sys.argv[sys.argv.index("--query") + 1]
        skills = build_index()
        print(match(text, skills) or "(no matches)")
        return
    if "--judge" in sys.argv:
        # Candidates with descriptions, for the agent hook that reranks them.
        text = sys.argv[sys.argv.index("--judge") + 1]
        skills = build_index()
        by_name = {s["name"]: s for s in skills}
        hits = match(text, skills)
        if not hits:
            print("NO CANDIDATES")
            return
        for name in hits:
            print(f"{name} :: {by_name[name]['desc'][:280]}")
        return

    try:
        payload = json.load(sys.stdin)
    except ValueError:
        return
    prompt = extract_prompt(payload)
    if not prompt.strip():
        return
    hits = match(prompt, build_index())
    if not hits:
        return
    print(json.dumps({"hookSpecificOutput": {
        "hookEventName": "UserPromptSubmit",
        "additionalContext": (
            "Possibly relevant skills (local keyword match, not judgement): "
            + ", ".join(hits)
            + ". Invoke via the Skill tool if one genuinely fits; ignore otherwise."
        ),
    }}))


if __name__ == "__main__":
    if len(sys.argv) > 1:
        main()             # manual invocation: let errors surface, that's the point
    else:
        try:
            main()
        except Exception:  # a broken matcher must never block the user's prompt
            sys.exit(0)
