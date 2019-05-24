# Lint

We use clang-tidy to lint C++ code and aim to keep the repository warning-clean.
The linter is configured in the [.clang-tidy](/.clang-tidy) file.

## How to lint

In order to run a specific GN target through the linter, run:

```
fx clang-tidy --target=<target>
```

You can also add `--fix` in order to automatically generate fixes for some (but
not all) of the warnings.

Alternatively, you can run the following to run the current patch through the
linter:

```
../scripts/git-file-tidy [--out-dir out/debug-x64]
```

In order to run the entire repository through the linter, add `--all`. You can
also add `--fix` in order to automatically generate fixes for some (but not all)
of the warnings.

## Suppressing warnings

Any warning can be suppressed by adding a `// NOLINT(<check_name>)` or a
`// NOLINTNEXTLINE(<check_name>)` comment to the offending line. It is also
possible to disable the check entirely within the repository by editing the
[.clang-tidy](/.clang-tidy) file.

## Checks

There are a number of check categories enabled, and specific checks within them
have been disabled for the reasons below. The list of enabled check categories
is as follows:

 - `bugprone-*`
 - `clang-diagnostic-*`
 - `google-*`
 - `misc-*`
 - `modernize-`
 - `performance-*`
 - `readability-*`

This list tracks the reasons for which we disabled in particular [checks]:

 - `clang-diagnostic-unused-command-line-argument` - ninja-generated compilation
    database contains the linker argument which ends up unused and triggers this
    warning for every file
 - `misc-noexcept*` - Fuchsia doesn't use C++ exceptions
 - `modernize-deprecated-headers` - Fuchsia uses old-style C headers
 - `modernize-raw-string-literal` - the check was suggesting to convert `\xFF`
    literals, which we'd rather keep in the escaped form.
 - `modernize-return-braced-init-list` - concerns about readability of returning
    braced initialization list for constructor arguments, prefer to use a
    constructor explicitly
 - `modernize-use-equals-delete` - flagging all gtest TEST_F
 - `modernize-use-equals-default` - Fuchsia chose not to impose a preference for
   "= default"
 - `performance-unnecessary-value-param` - it was flagging view classes
   which we prefer to pass by value
 - `readability-implicit-bool-conversion` - Fuchsia C++ code commonly uses implicit
   bool cast of pointers and numbers
 - `readability-isolate-declaration` - Zircon code commonly uses paired declarations.
 - `readability-uppercase-literal-suffix` - Fuchsia C++ code chooses not to impose
   a style on this.

[checks]: https://clang.llvm.org/extra/clang-tidy/checks/list.html
