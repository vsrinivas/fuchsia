# Static Analysis Tools

`staticanalysis` contains Go wrappers around various static analysis tools
(linters, auto-formatters, etc.) to transform their outputs into a consistent
format.

`staticanalysis` and its dependent tools are built using the Fuchsia build
system and assumes that it has access to a full Fuchsia checkout and build
directory. Although this is subject to change in the future as they may be
generalized to work out-of-tree.

## `staticlints`

The `staticlints` tool runs selected analyzers in non-blocking mode.

The data emitted by this tool feeds into the LUCI
[Tricium](https://goto.corp.google.com/tricium) service that Fuchsia uses to add
non-blocking robot comments on pending changes.

## Integrating a new tool

If you want to integrate a new static analysis tool called "foofmt", add a new
`foofmt.go` file in this directory that contains a `FooAnalyzer` type
implementing the `Analyzers` interface. Then add `FooAnalyzer` to the list of
analyzers run in either `staticchecks` (if the tool should block presubmit) or
`staticlints` (if the tool should emit non-blocking comments).
