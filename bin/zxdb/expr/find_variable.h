// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <optional>
#include <string>

namespace zxdb {

class CodeBlock;
class Collection;
class Identifier;
class FoundMember;
class FoundVariable;

// Main variable finding function. Searches the local, "this", and global
// scopes for a variable with the given identifier name.
//
// The block identifiers the place to search from. It can be null in which case
// only the global scope will be searched.
std::optional<FoundVariable> FindVariable(const CodeBlock* block,
                                          const Identifier& identifier);

// Type-specific finding -------------------------------------------------------

// Searches the give code block for local variables. This includes all nested
// code blocks and function parameters, but does not go into the "this" class
// or any non-function scopes like the current or global namespace (that's
// what the later functions do).
std::optional<FoundVariable> FindLocalVariable(const CodeBlock* block,
                                               const Identifier& identifier);

// Searches for the given variable name on the given collection. This is the
// lower-level function and assumes a valid object.
std::optional<FoundMember> FindMember(const Collection* object,
                                      const Identifier& identifier);

// Attempts the resolve the given named member variable on the "this" pointer
// associated with the given code block. Fails if the function has no "this"
// pointer or the member isn't found.
std::optional<FoundVariable> FindMemberOnThis(const CodeBlock* block,
                                              const Identifier& identifier);

}  // namespace zxdb
