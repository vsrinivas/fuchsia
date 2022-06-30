# Test application for the pbms lib

This doesn't have all of the features of the product-bundle command (doesn't
talk to the target device, for example), it's helpful to have a more rapid
development cycle, since this builds faster.

If/when the build time of the ffx tool is faster, this tool can be removed.

This tool is intended for rapid development only. It is not intended to replace
an ffx sub-command.

## Development of PBMS Lib

When working on product_test_tool, consider using:

```
$ fx set [...] --with-host //src/developer/ffx/lib/pbms/test
```

### Unit Tests

Unit tests can be run with:

```
$ fx test product_test_tool_bin_test
```
