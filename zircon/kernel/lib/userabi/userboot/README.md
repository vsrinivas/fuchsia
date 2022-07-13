# Userboot

## Svc Stub

Processes launched by userboot will be provided with a handle of `PA_NS_DIR`
type and name '/svc'. This allows services to pipeline requests to services
that might eventually be available. For now, this is only used for pipelining
requests to `fuchsia.debugdata.Publisher`. The server side of the handle is
stashed in the `Svc Stash`.

## Svc Stash

The `Svc Stash` corresponds to the `fuchsia.boot.SvcStash` protocol. Essentially
the server end of the handle is propagated to booted program through the handle
table as `PA_USER0` with argument `0`.

Userboot will issue one `fuchsia.boot.SvcStash/Push` for each launch program,
including the booted program itself. For example, if userboot would launch a
test and then the booting program, it would issue a push for the test and
the booting program. Each with their respective handles.
