# `stash_ctl`

Reviewed on: 2019-07-22

`stash_ctl` exists to be a reference implementation for
[stash](../stash/README.md). It's a command line tool that always reports its
identity as `stash_ctl` to the stash service, thus making it unsuitable for
interacting with data store in stash by other clients.

## Building

To add this project to your build, append `--with //src/sys/tools/stash_ctl`
to the `fx set` invocation.

## Running

`stash_ctl` is accessible via the shell:

```
$ fx shell run stash_ctl --help
```

## Testing

There are no unit tests for `stash_ctl`. It can be manually tested against the
stash service.

```
$ fx shell run stash_ctl set foo int 3
$ fx shell run stash_ctl get foo
```

## Source layout

The implementation is located in `src/main.rs`.
