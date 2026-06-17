// Luna Build — Lua frontend for ninja.
//
// Added via graft overlay (a brand-new file, no patch to existing ninja code).
// Declares the single hook the ninja.cc patch calls instead of the old
// ManifestParser::Load(): parse a .luna (Lua) file into ninja's State.

#ifndef NINJA_LUNA_H_
#define NINJA_LUNA_H_

#include <string>

struct ManifestParser;

/// Execute the Lua build file `filename`, translating its declarations into
/// ninja manifest text, then hand that text to ninja's own ManifestParser so
/// all graph-building/correctness logic is reused unchanged.
/// Returns false and fills `err` on failure.
bool LunaLoad(ManifestParser& parser, const std::string& filename,
              std::string* err);

#endif  // NINJA_LUNA_H_
