# Subpackage Examples

The following directory hosts examples resolving components from
[Subpackages (RFC-0154)](/docs/contribute/governance/rfcs/0154_subpackages.md).
These examples are also the source code references used in the
[developer guide](/docs/development/testing/components/subpackaging.md).

<!-- TODO(fxbug.dev/102652): Add the document at the above link -->

The tests are meant to demonstrate compilable code. This means that
it will always contain the latest API surface of each of the client libraries.

## Building

If these components are not present in your build, they can be added by
appending `--with //examples:tests` to your `fx set` command. For example:

<!--
TODO(fxbug.dev/102652): Use the following more common example, instead of the
one below, when the feature flag is no longer needed:

$ fx set core.x64 --with //examples --with //examples:tests
-->

```bash
$ fx set core.x64 --args='full_resolver_enable_subpackages=true' \
    --with //examples/components/subpackages:tests
$ fx build
```

## Testing

Run the tests for all languages using the `subpackage-examples` package.

```bash
$ fx test subpackage-examples
```