# `cs`

Reviewed on: 2019-07-22

`cs` is a command line tool intended for developers that will print out the
current state of the v1 component topology as run under
[appmgr](../appmgr/README.md)

## Building

To add this project to your build, append `--with //src/sys/tools/cs` to the
`fx set` invocation.

## Running

`cs` can be invoked directly with the shell.

```
$ fx shell cs
```

## Testing

`cs` can be manually tested by invoking it with the shell.

## Source layout

The entrypoint is located in `src/main.rs`, and an implementation for reading
inspect data is located in `src/inspect.rs`.
