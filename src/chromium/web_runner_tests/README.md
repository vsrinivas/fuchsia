# `web_runner_tests`

Contains integration tests to ensure that the Chromium version rolled into
[`//topaz/tools/cipd.ensure`](../../tools/cipd.ensure) is compatible with
Fuchsia. To run these tests, use:

```
fx set core.<board> --with //topaz/bundles:buildbot
fx full-build
fx reboot -r
fx run-tests web_runner_tests
```

To run individual test suites, you can use:

```
fx run-tests web_runner_tests -t web_runner_integration_tests
fx run-tests web_runner_tests -t web_runner_pixel_tests
```

In particular the pixel tests must be run a product without a graphical base
shell, such as `core`.

For more information about the individual tests, see their respective file
comments.