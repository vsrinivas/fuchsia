# Conventions

This document tracks layer-wide conventions for Peridot code.

## Testing helpers

Test-only utils (but not the tests themselves) related to a given library should
be placed under the directory of the library itself, in a subdirectory called
`testing`. (and not `test`)

The test-only symbols should be declared in the same namespace as the library
the test helpers are related to. (and not in a `test` or `testing` subnamespace)

The build targets containing  test-only utils should be marked as such using the
GN `testonly = true` annotation.
