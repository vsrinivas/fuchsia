# SL4F

The core of the project is an HTTP server called Scripting Layer for Fuchsia
(SL4F), which runs on Fuchsia devices. The server processes JSON RPC requests
from remote host driven test frameworks in the form of core service commands,
parses these requests, and fulfills them with the appropriate Fuchsia
equivalent (FIDL) commands.

## SL4F Includes

To include SL4F by default to the build include it with your build:
`--with src/testing/sl4f:bin`

## Pushing incremental changes

1. Build with this command `fx build`.
2. Terminte previous instances of sl4f: `fx shell killall sl4f.cmx`.
3. Start a new sl4f instance: `fx run sl4f.cmx`.

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
201: fx_log_info!(tag: &with_line!(tag), "{:?}", "Really important log.");
```
This outputs to
```
GenericFacade::func:201 Really important log.
```
