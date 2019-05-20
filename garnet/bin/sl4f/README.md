SL4F
=========

The core of the project is an HTTP server called Scripting Layer for Fuchsia
(SL4F), which runs on Fuchsia devices. The server processes JSON RPC requests
from remote host driven test frameworks in the form of core service commands,
parses these requests, and fulfills them with the appropriate Fuchsia
equivalent (FIDL) commands.


## Getting Started

## SL4F Includes
To include SL4F by default to the build include it with your build:
`--with garnet/bin/sl4f:bin`


## Pushing incremental changes
1. Make sure SL4F is running.
2. Build and push with this command `fx build-push sl4f`.
3. Before changes will take effect make sure there are no actively running SL4F
instances. Within `fx shell` run `killall sl4f.cmx`.
4. Run SL4F within an `fx shell` instance:
`run fuchsia-pkg://fuchsia.com/sl4f#meta/sl4f.cmx`. Note that you can tune your
test framework to open an ssh connection to run SL4F in the background.

## Facades
Facades is a term used in SL4F to represent the actual wrapper for FIDL apis
and maintain statefulness of the context. For example if there is an
asyncronous event the Facade will keep track of anything important to verify
for testing at a later time.

## Logging
In `garnet/bin/sl4f/server/sl4f.rs` there is a macro called `with_line`. Use
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
