# Realm Builder Examples

The following directory hosts example of usages of the Realm Builder library.
These examples are also the source code references used in the
[developer guide](/docs/development/testing/components/realm_builder.md).

The tests are meant to demonstrate compilable code. This means that
it will always contain the latest API surface of each of the client libraries.

## Building

If these components are not present in your build, they can be added by
appending `--with //examples` to your `fx set` command. For example:

```bash
$ fx set core.x64 --with //examples --with //examples:tests
$ fx build
```

## Testing

Run the tests for all languages using the `realm-builder-examples` package.

```bash
$ fx test realm-builder-examples
```