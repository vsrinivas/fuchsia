# Additional Inputs for Build-Time Verification

## Verify Routes Exceptions Allowlist(s)

Files named `verify_routes_exceptions_allowlist*.json` are allowlists for the
_verify routes_ scrutiny command/verifier. They are integrated into the build
via the `verify_routes()` gn template and setting gn args that begin with
`fuchsia_verify_routes_exceptions_allowlist`.
