# FIDL Cross-Petal Change Tests

These are tests to validate source-compatibility through various changes to FIDL
libraries. They do not test binary-compatibility. Tests for compiled languages
are only meant to be built, not executed, since source-compatibility issues show
up at compile time.

## Transitions

Soft transitioning a FIDL library involves an initial state (**before**), an
intermediate state (**during**), and a final state (**after**). Depending on
the nature of the change, the bindings used and how the bindings are used
source code changes will need to be made at a few different points:
- early in the transition (FIDL **before** & **during** states)
- late in the transition (FIDL **during** & **after** states)
- cleanup after the transition (FIDL **after** state)

## Tests

A test is declared by writing a test JSON configuration and adding a `source_compatibility_test` target to the build.