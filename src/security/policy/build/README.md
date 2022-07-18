# Additional Inputs for Build-Time Verification

## Verify Routes Exceptions Allowlist(s)

Files named `verify_routes_exceptions_allowlist*.json` are allowlists for the
_verify routes_ scrutiny command/verifier. They are integrated into the build
via the `verify_routes()` gn template and setting gn args that begin with
`fuchsia_verify_routes_exceptions_allowlist`.

## Verify Against Golden and Build Type

The `verify_golden_by_build_type.py` script verifies a file against a golden,
selecting the appropriate golden based on the build type specified in a product
configuration file. It is integrated into the build via the
`verify_file_by_build_type()` and `verify_structured_config_by_build_type()` gn
templates.

### Verifying Structured Configuration

Structured configuration goldens are stored in `*_golden_non_eng.cvf` and
`*_golden_eng.cvf` files, for verification against non-eng and eng build types,
respectively. These files are integrated into the build via the
`verify_structured_config_by_build_type()` gn template.
