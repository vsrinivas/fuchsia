# Lint

We use clang-tidy to lint C++ code.

The linter is configured in the [.clang-tidy](../.clang-tidy) file.

## Disabled checks

This list tracks context on why we disabled particular checks:

 - `clang-diagnostic-unused-command-line-argument` - ninja-generated compilation
    database contains the linker argument which ends up unused and triggers this
    warning for every file
 - `misc-noexcept*` - Fuchsia doesn't use C++ exceptions
 - `modernize-deprecated-headers` - Fuchsia uses old-style C headers
 - `modernize-return-braced-init-list` - concerns about readability of returning
    braced initialization list for constructor arguments, prefer to use a
    constructor explicitly
 - `modernize-use-auto` - not all flagged callsites seemed worth converting to
    `auto`
 - `modernize-use-equals-delete` - flagging all gtest TEST_F
 - `modernize-use-equals-default` - Ledger chose not to impose a preference for
   "= default"
 - `performance-unnecessary-value-param` - it was flagging Ledger view classes
   which we prefer to pass by value
 - `readability-implicit-bool-cast` - Fuchsia C++ code commonly uses implicit
   bool cast of pointers and numbers


