# Diagnostics Tool

Reviewed on: 2019-12-17

The Diagnostics Tool is a utility for interacting with diagnostics data
on a Fuchsia system.

The Diagnostics Platform exposes filtered views of all diagnostics
data on a system, subject to the constraints in selector files. This
tool currently provides an interactive terminal UI to aid writing the
selector configuration.

## Building

This project can be added to builds by including `--with //src/diagnostics/tool:diag_tool_host`
to the `fx set` invocation.

For example:

```
fx set core.chromebook-x64 --with //src/diagnostics/tool:diag_tool_host
```

## Running

From your Fuchsia directory:

```
# Based on an input Inspect dump, generate a selector file exactly
# matching all keys found.
./out/default/host-tools/diag_tool -b <inspect JSON> generate <output file>

# Interactively apply the input selector file to the JSON.
# You may edit the input file and press "R" to reload. Lines that would be 
# filtered out are highlighted red (you may hide them by pressing "H").
./out/default/host-tools/diag_tool -b <inspect JSON> apply <input file>
```

You can obtain an initial Inspect dump from a running system using:
```
fx iquery --format json > <output file>
```

## Testing
To run unit tests:
```
fx set ... --with //src/diagnostics/tool:diag_tool_tests
fx run-test inspect_validator_tests
```
