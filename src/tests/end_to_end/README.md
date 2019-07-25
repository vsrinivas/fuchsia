# End to end product tests

End to end product tests for fuchsia products are located here.

Unlike other kinds of tests, end to end product tests are specific to a product,
and therefore must not be included in the global `tests` build target mandated
by the [source tree layout](../../docs/development/source_code/layout.md).
Instead, end to end product tests are included directly in the build
configuration of their products in [//products](../../products/).

# Adding your end-to-end test directory

Please also add an `OWNERS` file so that you can edit your test at will. If
there are run-time dependencies needed for the test you can add them to the
`/bundles:end_to_end_deps` group.

# Running an end-to-end test

Because end to end tests are tied to a specific product or even architecture,
you should review the test's README.md to see which product or special
instructions are needed to run the test.

To run an end to end test use the `run-e2e-test` command:

```
$ fx set <product>.<arch> && fx build
$ fx run-e2e-test name_of_the_test
```

Where `name_of_the_test` is the name of the dart_test rule (like `sl4f_test`).
