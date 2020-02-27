# `stash`

Reviewed on: 2020-02-26

Stash exists to hold persistent mutable state for early boot system services
that are restricted from using mutable storage (usually for security reasons).
Stash may be used to store device-wide state. Stash must not be used to store
user-specific state, since data isn't saved to a user-encrypted partition.

Persisted state takes the form of a key/value store, which can be accessed over
FIDL. More details on writing a program that uses stash is available
[here](stash.md).

## Building

This project can be added to builds by including `--with //src/sys/stash` to
the `fx set` invocation.

## Running

Stash provides the `fuchsia.stash.Store` and `fuchsia.stash.SecureStore` service
on Fuchsia, and there is a `stash_ctl` command to demonstrate how to access
these services.

```
$ fx shell run stash_ctl --help
```

## Testing

Unit tests for stash are available in the `stash_tests` package.

```
$ fx run-test stash_tests
```

## Source layout

The entrypoint is located in `src/main.rs`, the FIDL service implementation
exists in `src/instance.rs` and `src/accessor.rs`, and the logic for storing
bytes on disk is located in `src/store.rs`. Unit tests are co-located with the
implementation.
