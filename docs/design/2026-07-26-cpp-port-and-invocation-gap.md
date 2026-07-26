# skill-matcher: C++ port, louder output, and a hit rate

**Date:** 2026-07-26
**Status:** approved, implementing

## The problem this is actually solving

The matcher works. `--test` reports `188 skills indexed, 25 name lookups + 8 silences`, and a
July rewrite fixed four genuine scoring defects (term frequency discarded, unbounded synonym
fan-out, alias collapse, ~12 built-in skills invisible), taking the index 244 → 214.

Two days after that rewrite the owner still called it useless. So match quality was never the
binding constraint. The recorded conclusion:

> it can only **suggest** and I never **invoke**, so a correct suggestion I skip looks exactly
> like a dead hook.

A third failure now sits on top of that: **it is installed nowhere**. Not in
`settings.json`, not in `installed_plugins.json`, and `~/.claude/hooks/skill-matcher.py` — the
path its own memory note records as live — does not exist. Last genuine injection: Jul 24
18:17. Improving a hook that fires zero times would repeat the July outcome exactly.

## Non-goals

- **No further scoring work.** That is what July did, measured, and it did not move the
  owner's experience. Adding concept groups is tuning, not a fix.
- **No injecting skill bodies.** Considered and rejected as too token-expensive for the gain.
- **No third layer.** The standing instruction is to prune what never fires, not to stack
  another system on top of one that already does not.

## 1 — C++ port

Python costs ~150 ms per prompt, nearly all of it interpreter startup, on a hook that fires on
**every message typed**. Beyond speed, the repo belongs to an owner whose rule is C++ by
default; a Python hook in it is the same contradiction metal had.

~404 logic lines split along seams that already exist, every file inside metal's 300-line rule:

| file | job |
|---|---|
| `src/main.cpp` | hook I/O: read stdin, emit `additionalContext`, fail open |
| `src/prompt.{cpp,hpp}` | tokenize, stem, chatter filter |
| `src/concepts.{cpp,hpp}` | the 48 concept groups (data) |
| `src/index.{cpp,hpp}` | walk skill dirs, parse frontmatter, alias-collapse, IDF |
| `src/score.{cpp,hpp}` | scoring, thresholds, selection |
| `src/selftest.inc` | assertions, compiled only under `-DSELFTEST` |

No JSON library and no regex library — hand scanners, as in metal. Every failure path exits 0:
a hook that dies must never block a prompt.

**Behaviour is frozen for the port.** The scoring constants, thresholds, stemmer, concept
groups, alias rule and builtins list all carry over unchanged, so the port can be verified
differentially rather than by eye. Output shape changes only in step 2, after the port is
proven identical.

## 2 — Fewer, louder, shaped as an instruction

A list of six names is a menu, and menus get skimmed. The observed failure is not that the
right skill was absent from the line; it is that the line read as optional.

```
now        Possibly relevant skills: transitions-dev, fixing-motion-performance,
           to-spring-or-not-to-spring, interaction-design, emil-design-eng, …

decisive   This is a motion problem — use transitions-dev.
ambiguous  Two skills fit: transitions-dev or fixing-motion-performance. Pick one.
weak       (silence)
```

- `kMaxHits` 6 → 2.
- New `kDecisive` ratio: the top hit must beat the runner-up by this margin to speak as a
  single instruction rather than offer a choice.
- Silence thresholds unchanged. Firing on chatter is what teaches the reader to ignore the
  line, and that is the failure being fixed.

## 3 — A hit rate

Nothing currently knows whether a suggestion led to an invocation, which is why the argument
about whether this works has run on feelings for a week.

- `UserPromptSubmit` appends what it suggested.
- `PostToolUse` on `Skill` appends what was actually invoked. **This is a second hook the
  plugin does not currently ship.**
- `skill-matcher --stats` joins them by session: suggested N, followed M, rate M/N.

Storage is one append-only TSV under `~/.claude/skill-matcher/`. No database, no daemon.

This is the only part that can tell us whether step 2 worked. Without it the next round of
"feels useless" has nothing to argue with.

## 4 — README

Raised to the standard of the owner's other two repos: authored SVG hero (dark and light),
badges, nav, a before/after match table, folds for layout and design notes. The existing
"What it doesn't do" section is kept as-is — it is the most honest section in the repo.

## Verification

1. **Selftest** ported, plus assertions for the decisive/ambiguous/silent split.
2. **Differential against the Python** on real prompts and the existing test corpus, compared
   as parsed output. The same method found zero mismatches across 1,180 inputs on metal, and
   caught a formatting-only diff that would have read as 274 failures.
3. **Installed, then measured.** The hit rate from step 3 is the acceptance criterion, and it
   can only be collected in real use.

## Success criteria

- Differential run against the Python: zero semantic mismatches, before output changes.
- Every source file inside 300 lines; selftest green; `-Wall -Wextra -Werror` clean.
- Startup measurably below the Python's ~150 ms.
- The hook is installed and firing — verified by a non-zero suggestion count in the log, not
  by inspection.
- `--stats` reports a real hit rate after a day of use.

## Risks

- **A louder hook is more annoying when wrong.** Mitigated by holding the silence thresholds
  and capping at two, but if precision drops the fix is the threshold, not the volume.
- **The hit rate can only be measured live**, so step 2's benefit is unproven until it has run
  for a while. That is the point of shipping step 3 alongside it rather than after.
