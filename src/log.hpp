// Suggested-vs-invoked, so "it feels useless" becomes a number.
//
// The matcher was rewritten once on the assumption that its matches were bad. They were not;
// the problem was that a correct suggestion nobody acts on looks exactly like a dead hook.
// There was no way to tell those apart, so the argument ran on feelings for a week. This is
// the cheapest possible instrument: two append-only lines and a join.
#pragma once

#include <string>
#include <vector>

namespace sm {

// One line per prompt that produced output: what was offered, and how loudly.
void log_suggestion(const std::string& session, const std::vector<std::string>& names,
                    bool decisive);

// One line per Skill invocation, from the PostToolUse hook.
void log_invocation(const std::string& session, const std::string& skill);

// Joins the two by session: of the skills offered, how many were then invoked.
// Printed by --stats. Returns 0 always; failure to read is reported, not thrown.
int print_stats();

}  // namespace sm
