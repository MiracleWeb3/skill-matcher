# skill-matcher

I had 244 skills installed and was using about six of them.

Not because the rest were bad. Because Claude never noticed they were there. Every skill's
description sits in context, but picking the right one out of hundreds is a recall problem,
and when recall fails it fails silently — no error, no "I couldn't find a skill", just a
normal answer that would have been better with the skill you installed for exactly that.

This is a hook. On every prompt it scores your message against every installed skill and
injects the top matches, so the choice gets put in front of Claude instead of left to memory.

No LLM call, no network, no tokens. About 150ms of local Python.

## Install

```
/plugin marketplace add MiracleWeb3/skill-matcher
/plugin install skill-matcher
```

Needs `python3`. Nothing else — no pip install, no dependencies.

## What it does

```
the sidebar collapse feels janky
  -> transitions-dev, fixing-motion-performance, to-spring-or-not-to-spring

make this accessible
  -> fixing-accessibility, wcag-audit-patterns, a11y-debugging

what did we decide about the VPN setup
  -> memory-search, claude-memory-light

thanks, looks good
  -> (nothing)
```

That last one matters as much as the others. A hook that fires on every message gets tuned
out the moment it starts guessing, so below threshold it prints nothing.

## How it matches

Three layers, all local:

**IDF scoring.** Rare words count, common ones don't. "oklch" is a strong signal; "use" and
"when" appear in half the descriptions and are worth nearly nothing. A skill qualifies if the
prompt hits its *name*, or hits two or more words you actually typed — one generic word in a
description is not a signal.

**Stemming.** "accessible" and "accessibility" have to meet somewhere, or you get zero matches
while owning five accessibility skills. Same for animated/animation, optimize/optimization,
decide/decided. A ~25-line suffix stripper, applied to both sides.

**Concept groups.** Stemming can't get you from "janky" to "performance" — different words, not
different forms. So there's a list of concept groups where every member bridges to every other:
48 groups, ~390 words, ~3200 bridges. Words you type count full; words inferred from a group
count half.

## Tuning it

When it misses, that's a word, not an algorithm. Add it to the matching line in `CONCEPTS` at
the top of `scripts/skill-matcher.py`:

```python
["animation", "motion", "transition", "easing", "smooth", "snappy", ...],
```

Then check it:

```
python3 scripts/skill-matcher.py --query "the thing that missed"
python3 scripts/skill-matcher.py --list      # what got indexed
python3 scripts/skill-matcher.py --test      # regression suite
```

The suite builds itself from your own installed skills — asking for a skill by its own name
must return it — plus a set of chatter phrases that must stay silent.

## What it doesn't do

It suggests, it doesn't decide. Claude still picks, and can still ignore the list. This raises
recall; it doesn't guarantee it.

It can only suggest skills you have. Ask why a SQL query is slow with no database skill
installed and you'll get frontend performance skills, because those are the only slow-related
things on the machine.

It matches words, not meaning. Roughly right in the top few, noise in the tail. The trade is
deliberate: a wrong suggestion costs one glance, a missing one costs a skill you never use.

## License

MIT
