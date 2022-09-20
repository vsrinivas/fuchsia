# SL4F

The core of the project is an HTTP server called Scripting Layer for Fuchsia
(SL4F), which runs on Fuchsia devices. The server processes JSON RPC requests
from remote host driven test frameworks in the form of core service commands,
parses these requests, and fulfills them with the appropriate Fuchsia
equivalent (FIDL) commands.

## Build SL4F

To include SL4F by default in the build include it with your build:
`--with //src/testing/sl4f`. Note that SL4F is only supported on `*_eng` products
like `workstation_eng`. `core` is not supported.

## Start SL4F component

Start a new SL4F component instance with `ffx component start /core/sl4f`.

## Pushing incremental changes

1. Build with `fx build`.
2. Reload the SL4F component with `ffx component reload /core/sl4f`.

**Note**: you can tune your test framework to open an ssh connection to run
SL4F in the background.

## Facades

Facades in SL4F are wrappers around FIDL APIs and maintain stateful context
for successive interaction with services and enable introspection of state
for making assertions. For example a facade may record asyncronous events
making them available for a test to later verify.

## Logging

In `src/testing/sl4f/server/sl4f.rs` there is a macro called `with_line`. Use
this with your tag in your log lines.

Example:
```
200: let tag = "GenericFacade::func";
201: tracing::info!(tag = &with_line!(tag), "{:?}", "Really important log.");
```
This outputs to
```
GenericFacade::func:201 Really important log.
```

# Deleted facades


## adc
- CL: https://fxrev.dev/631770

## battery_simulator
- CL: https://fxrev.dev/631362

## boot_arguments
- CL: https://fxrev.dev/631771

## cpu_ctrl
- CL: https://fxrev.dev/631364

## repository_manager
- CL: https://fxrev.dev/635304

## update
- CL: https://fxrev.dev/631268
