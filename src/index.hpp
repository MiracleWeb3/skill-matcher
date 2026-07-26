// Building the index: every skill the model can actually invoke right now.
//
// Three sources, and the third is the one people forget: user skills, skills from enabled
// plugins, and ~12 skills that ship inside Claude Code with no SKILL.md anywhere on disk.
// Those last ones were never once suggestible until they were hand-listed — and they include
// the ones you would most want offered, like code-review and dataviz.
#pragma once

#include <string>
#include <unordered_set>
#include <vector>

namespace sm {

struct Skill {
    std::string name;                          // "plugin:slug" or "slug"
    std::string desc;
    std::unordered_set<std::string> name_terms;
    std::unordered_set<std::string> terms;     // name + description
};

// Pulls the description out of a SKILL.md frontmatter block. Empty when there is no
// frontmatter or no description — such files are skipped rather than indexed blank.
std::string read_description(const std::string& path);

// Content identity, so a rename collapses into the skill it was renamed from. An aggregator
// vendoring another plugin under a new name is invisible to slug matching, and both copies
// would otherwise eat a suggestion slot. Empty when the description is too thin to judge.
std::unordered_set<std::string> fingerprint(const std::string& slug, const std::string& desc);

// Renamed twins overlap at .90 and up, genuine rewrites of one idea sit near .03, so the call
// in between is one this never has to make.
bool same_skill(const std::unordered_set<std::string>& a, const std::unordered_set<std::string>& b);

std::vector<Skill> build_index();

}  // namespace sm
