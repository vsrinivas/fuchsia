# `stash`

Reviewed on: 2022-01-13

Stash exists to hold persistent mutable state for early boot system services
that are restricted from using general mutable storage (usually for security
reasons). Persisted state takes the form of a key/value store, which can be
accessed over FIDL.

Multiple instances of stash are provided, each serving a different
`fuchsia.stash` protocol. An instance of stash cannot securely identify the
clients connecting to it and therefore cannot guarantee isolation between those
clients. This means that the clients of each protocol must be carefully reviewed
to assess the impact of any compromise in one client on the other clients.

It is likely that stash will be deprecated and new clients are no longer being
accepted.

## Building

To add this project to your build, append `--with //src/sys/stash` to the
`fx set` invocation.

## Running

Stash provides the `fuchsia.stash.Store`, `fuchsia.stash.Store2`, and
`fuchsia.stash.SecureStore` services on Fuchsia, and there is a `stash_ctl`
command to demonstrate how to access these services.

```
$ fx shell run stash_ctl --help
```

## Testing

Unit tests for stash are available in the `stash-tests` package.

```
$ fx test stash-tests
```

## Source layout

The entrypoint is located in `src/main.rs`, the FIDL service implementation
exists in `src/instance.rs` and `src/accessor.rs`, and the logic for storing
bytes on disk is located in `src/store.rs`. Unit tests are co-located with the
implementation.
