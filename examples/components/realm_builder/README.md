# Realm Builder Examples

The following directory hosts example of usages of the Realm Builder library.
This is primarily for code references used in the official docs (found at
//docs/development/testing/components/realm_builder.md), though it serves as a useful guide
on its own. The tests are meant to demonstrate compilable code. This means that
it will always contain the latest API surface of each of the client libraries.
However, the tests are disabled because they construct invalid realms for demonstration
purposes.

## Building

If these components are not present in your build, they can be added by
appending `--with //examples` to your `fx set` command. For example:

```bash
$ fx set core.x64 --with //examples --with //examples:tests
$ fx build
```

## Testing

Run the tests for cpp using the `realm-builder-examples` package.

```
$ fx test realm-builder-examples
```