
# Test Adapters

Test Adapters are a trampoline that launch test binaries, parses output and translates it to fuchsia.test.Suite protocol. This would eventually be ported into a runner. We are planning to add adapters for rust and gtests(C++).

## Building

```bash
fx set core.x64 --with //src/sys/test_adapters
fx build
```

## Running

TODO: Add information here about how to integrate this adapter into tests.

## Testing

To test gtest\_adapter run:

```bash
fx run-test gtest_adapter_tests
```
