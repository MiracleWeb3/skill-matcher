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
MIN_SINGLE_TERM = 8.0    # floor when a single typed word is the entire case for a skill
NAME_COVERAGE = 0.34     # share of typed terms a lone name hit must carry to count as strong
MIN_FINGERPRINT = 6      # tokens needed before two descriptions may be called the same skill
ALIAS_SIMILARITY = 0.85  # description overlap at which two names are one skill

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
    one a concept group inferred for you.

    Repetition counts. A prompt that says "bug" twice and "fix" once is about bugs, even
    when some rarer word wandered through it once -- dropping term frequency was what let
    a single mention of "dashboard" outrank the thing the whole message was asking for.
    Sublinear, because saying it three times doesn't make it three times the request."""
    weights = {}
    for t in terms:
        weights[t] = weights.get(t, 0.0) + 1.0
    for t, count in list(weights.items()):
        weights[t] = 1.0 + math.log(count)
    for t in list(weights):
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


# Skills that ship inside Claude Code itself. They have no SKILL.md anywhere on disk, so
# scanning can't see them -- and the ones you'd most want suggested (code-review, dataviz)
# were never once offered. Descriptions are trimmed from their own listings.
# ponytail: a hand-kept list, because there is nothing to read. Add a line when Claude Code
# ships a new built-in; a stale entry costs one suggestion, not a crash.
BUILTINS = [
    ("dataviz", "Create charts, graphs, plots, dashboards and data visualizations in any "
     "medium: HTML, SVG, matplotlib, plotly, d3, Recharts. Palettes, axes, legends, "
     "tooltips, stat tiles, heatmaps, sparklines, categorical and diverging colour."),
    ("code-review", "Review the current diff for correctness bugs and reuse, simplification "
     "and efficiency cleanups. Can post findings as inline pull request comments or apply "
     "them to the working tree."),
    ("simplify", "Review changed code for reuse, simplification, efficiency and altitude "
     "cleanups, then apply the fixes. Quality only, it does not hunt for bugs."),
    ("security-review", "Review code for security vulnerabilities, injection, broken auth, "
     "leaked secrets and unsafe patterns."),
    ("run", "Launch and drive this project's app to see a change working: start the app, "
     "screenshot it, confirm a change behaves in the real app and not only in tests."),
    ("claude-api", "Reference for the Claude API and Anthropic SDK: model ids, pricing, "
     "parameters, streaming, tool use, MCP, agents, prompt caching, token counting, "
     "model migration."),
    ("update-config", "Configure the Claude Code harness through settings.json: hooks for "
     "automated behaviour, permissions and allowlists, environment variables."),
    ("keybindings-help", "Customize keyboard shortcuts, rebind keys and add chord bindings "
     "in keybindings.json."),
    ("fewer-permission-prompts", "Scan transcripts for common read-only Bash and MCP calls, "
     "then write an allowlist into settings.json to cut permission prompts."),
    ("loop", "Run a prompt or slash command on a recurring interval, or let the model pace "
     "its own repeated iterations of a task."),
    ("schedule", "Create, update, list or run scheduled cloud agents and routines on a cron "
     "schedule, including one-off future runs."),
    ("artifact-design", "Design guidance and fundamentals for Artifacts, the shareable HTML "
     "or Markdown pages published and hosted for other people to view."),
]


def _fingerprint(slug, desc):
    """Content identity, so renames collapse into the skill they were renamed from.

    An aggregator plugin that vendors another plugin under a new name is invisible to
    slug matching -- 'spartan' and 'ponytail', 'sisyphus' and 'ralph' are byte-identical
    once each drops its own name, but nothing pairs them, so both eat a suggestion slot
    and you get two names for one skill. Subtracting the slug's own words leaves almost
    exactly the shared prose -- almost, because the stripping is asymmetric: "ralplan" is
    a single token, so its description keeps a "plan" that "sisyphus-plan" loses. Hence
    near-equality below, not equality. Genuine rewrites keep their own wording and survive
    as themselves.
    """
    terms = set(tokenize(desc)) - set(tokenize(slug))
    return terms if len(terms) >= MIN_FINGERPRINT else set()


def _same_skill(a, b):
    """Renamed twins overlap at .90 and up, real rewrites of one idea sit near .03, so
    the call in between is one this never has to make."""
    return len(a & b) >= ALIAS_SIMILARITY * len(a | b)


def build_index():
    """Every skill the model can actually invoke right now: user skills that aren't
    overridden off, plus skills from enabled plugins, plus the built-ins."""
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

    skills, seen, seen_content = [], set(), []

    def add(slug, desc, prefix=""):
        # Same skill vendored twice, by slug or by content; one suggestion is enough.
        # ponytail: ties go to whoever is scanned first -- own skills dir, then plugin
        # order -- which is stable and good enough while the copies say the same thing.
        if slug in seen:
            return
        fingerprint = _fingerprint(slug, desc)
        if fingerprint and any(_same_skill(fingerprint, o) for o in seen_content):
            return
        seen.add(slug)
        if fingerprint:
            seen_content.append(fingerprint)
        skills.append({
            "name": prefix + slug,
            "desc": desc,
            "name_terms": set(tokenize(slug)),
            "terms": set(tokenize(slug + " " + desc)),
        })

    for root, prefix in sources:
        for path in sorted(glob.glob(os.path.join(root, "*", "SKILL.md"))):
            slug = os.path.basename(os.path.dirname(path))
            if not prefix and overrides.get(slug) == "off":
                continue
            desc = read_description(path)
            if desc:
                add(slug, desc, prefix)

    for slug, desc in BUILTINS:
        if overrides.get(slug) != "off":
            add(slug, desc)
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
    typed = {t for t in query if query[t] >= 1.0}
    scored = []
    for s in skills:
        hits = set(query) & s["terms"]
        if not hits:
            continue
        name_hits = hits & s["name_terms"]
        # A name hit is strong evidence only when the prompt is actually ABOUT that word.
        # "dashboard" inside a twenty-word bug report is incidental -- the prompt is about
        # bugs -- while "ponytail" typed on its own is the whole request. Without this, one
        # rare word landing on a skill's name doubles its way past a skill that genuinely
        # matched four terms, and the junk that lands on top teaches you to skip the line.
        # Corroboration has to come from words you typed. Counting inferred ones lets a
        # single word vouch for itself: "dashboard" expands into the whole chart group,
        # which then matches a dashboard skill four ways and calls that four pieces of
        # evidence -- when the prompt said "dashboard chat" and meant a window.
        strong_name = bool(name_hits) and (
            len(hits & typed) >= 2 or len(name_hits & typed) >= len(typed) * NAME_COVERAGE)
        # ponytail: name evidence is all-or-nothing on purpose. Scaling the bonus by how
        # much of the prompt the name covers reads better and measures worse -- it starved
        # "build me a dashboard for these metrics" of the dashboard skill without rescuing
        # the incidental case it was meant to fix. If a name you typed in passing still
        # outranks your actual topic, weight names by term rarity here, not by coverage.
        said = inferred = 0.0
        for t in hits:
            idf = math.log((n - df[t] + 0.5) / (df[t] + 0.5) + 1.0)
            weight = NAME_WEIGHT if (strong_name and t in s["name_terms"]) else 1.0
            if t in typed:
                said += idf * weight * query[t]
            else:
                inferred += idf * weight * query[t]
        # Inferred evidence may support what you said, never outvote it. One word fans out
        # to a dozen synonyms, and a skill whose description happens to list all twelve was
        # collecting twelve counts of proof from a single passing mention -- enough to put a
        # charting skill on top of a bug report because the word "dashboard" went by once.
        score = said + min(inferred, said)
        # At least one word you actually typed, always -- a match built purely from inferred
        # synonyms is the concept table talking to itself. Past that, let the score decide:
        # it already carries rarity, repetition and name weight, and counting distinct terms
        # instead threw away the right answer whenever a prompt made its point with one word.
        if not (hits & typed):
            continue
        if strong_name:
            floor = MIN_NAME_SCORE
        elif len(hits & typed) == 1:
            # Resting on one word is fine when that word carries the prompt ("bug"), and
            # noise when it is ordinary English that a description happened to contain --
            # "what time is it" should not summon anything. Rarity can't tell those apart,
            # so make a lone word clear a bar the incidental ones never reach.
            floor = MIN_SINGLE_TERM
        else:
            floor = MIN_SCORE
        if score >= floor:
            scored.append((score, s["name"]))

    # Tie on score goes to the shorter name: typing "ponytail" wants the skill itself,
    # not ponytail-review, and reverse-sorting the tuple used to bury the parent under
    # every sub-skill that shares its name.
    scored.sort(key=lambda x: (-x[0], len(x[1]), x[1]))
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

    # The regression that made the whole line ignorable: one incidental word landing on a
    # skill's name ("dashboard chat" -- a window, not a chart) used to double past every
    # skill the prompt was genuinely about, so the junk on top taught you to skip the rest.
    report = ("difference between X chat and what is happening in dashboard chat: it did "
              "not read the conversation, did not pull all messages, and started answering. "
              "Another bug. Overall i see 3 bugs, right? Fix all.")
    hits = match(report, skills)
    # By description, not by name -- a renamed debugging skill is still a debugging skill,
    # and mythological aliases are exactly what this index is full of.
    desc_of = {s["name"]: s["desc"].lower() for s in skills}
    debugging = [i for i, h in enumerate(hits)
                 if re.search(r"\bbugs?\b|debug|diagnos", desc_of.get(h, ""))]
    passing = [i for i, h in enumerate(hits) if h.split(":")[-1] == "dashboard"]
    # It may still appear -- the word was typed -- but never above what was being asked.
    assert not passing or (debugging and min(debugging) < min(passing)), \
        f"incidental name word still outranking the topic: {hits}"

    # Aliases must have collapsed, or one skill occupies two slots under two names.
    prints = [p for p in (_fingerprint(s["name"].split(":")[-1], s["desc"]) for s in skills) if p]
    twins = [(a, b) for i, a in enumerate(prints) for b in prints[i + 1:] if _same_skill(a, b)]
    assert not twins, f"{len(twins)} skill(s) indexed twice under different names"

    print(f"ok - {len(skills)} skills indexed, {checked} name lookups + "
          f"{len(noise)} silences + alias collapse pass")


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
