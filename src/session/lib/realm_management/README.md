# realm_management

Reviewed on: 2021-01-21

`realm_management` is a library for adding child components to a specified realm. For more information on component realms, see [Realms (Components v2)](/docs/concepts/components/v2/realms.md).

## Building
To add `realm_management` to your build, append `--with //src/session/lib/realm_management` to the `fx set` invocation.

## Using
`realm_management` can be used by depending on the `//src/session/lib/realm_management` GN target.

`realm_management` is not available in the SDK.

## Testing
Unit tests for `realm_management` are available in the `realm_management_tests` package.

```shell
$ fx test realm_management_tests
```

## Source layout
The implementation is in `src/lib.rs`, which also includes unit tests.