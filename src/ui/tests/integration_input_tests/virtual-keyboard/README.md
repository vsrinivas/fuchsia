## Build the test

```shell
$ fx set <product>.<arch> --with //src/ui/tests/integration_input_tests/virtual-keyboard:tests
```

Note that some `product`s include a `web-engine`, which conflicts with the `web-engine`
bundled with `virtual-keyboard:tests`. To run this test on such `product`s, use the command
below instead:

```shell
$ fx set <product>.<arch> --with //src/ui/tests/integration_input_tests/virtual-keyboard:tests-product-webengine
```

## Run the test

To run the fully-automated test, use the following fx invocation. Note that the same command
works regardless of whether the build uses the product's web-engine, or a web-engine bundled
with the test.

```shell
$ fx test virtual-keyboard-test
```