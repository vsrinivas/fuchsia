# `component_index`

Reviewed on: 2019-07-11

`component_index` provides an implementation of the
[`fuchsia.sys.index.ComponentIndex`](fidl/index.fidl) service, which is used on
Fuchsia devices to convert human input into component URLs for developers.

See also: `//src/sys/locate`

## Building

This project can be added to builds by including `--with
//src/sys/component_index` to the `fx set` invocation.

## Running

Once included in a build, `component_index` can be accessed via tools such as
`locate` and `run`.

```
$ fx shell locate --help
```

## Testing

Unit tests for `component_index` are available in the `component_index_tests`
package.

```
$ fx run-test component_index_tests
```

## Source layout

The entrypoint is located in `src/main.rs`, and the fidl service is defined in
`fidl/index.fidl`.
