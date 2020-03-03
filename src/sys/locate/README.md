# `locate`

Reviewed on: 2020-03-02

`locate` is a convenience tool for expanding short names for components into
fully qualified [component URLs](/docs/concepts/components/component_urls.md).

`locate` uses data from the component index.
See also: `//src/sys/component_index`

For usage information, run `locate --help`.

## Building

This project can be added to builds by including `--with //src/sys/locate` to
the `fx set` invocation.

## Running

To print usage instructions, run the following command:

```posix-terminal
$ fx shell locate --help
```

## Testing

Integration tests for `locate` are available in the `locate_integration_tests`
package.

```posix-terminal
$ fx test locate_integration_test
```
