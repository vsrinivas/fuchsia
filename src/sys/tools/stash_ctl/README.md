# `stash_ctl`

Reviewed on: 2019-07-22

`stash_ctl` exists to be a reference implementation for
[stash](../stash/README.md). It's a command line tool that always reports its
identity as `stash_ctl` to the stash service, thus making it unsuitable for
interacting with data store in stash by other clients.

## Building

`stash_ctl` is packaged with the `stash` component. Include it with the following
`fx set` invocation:

```
> fx set <product>.<arch> --with //src/sys/stash --args=stashctl_enabled=true
```

## Running

`stash_ctl` is included in the `workstation_eng` product and is
accessible via `ffx component explore`:

```
> fx set workstation_eng.x64
> fx build
```

```
> ffx component explore /core/stash
$ stash_ctl --help
```

## Testing

There are no unit tests for `stash_ctl`. It can be manually tested against the
stash service.

```
> ffx component explore /core/stash
$ stash_ctl set foo int 3
$ stash_ctl get foo
```

## Source layout

The implementation is located in `src/main.rs`.
