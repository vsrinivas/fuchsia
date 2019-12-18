
# Test Adapters

Test Adapters are a trampoline that launch test binaries, parses output and
translates it to fuchsia.test.Suite protocol. This would eventually be ported
into a runner. We are planning to add adapters for rust and gtests(C++).

## Building

```bash
fx set core.x64 --with //src/sys/test_adapters
fx build
```

Above command will build all test adapters, to build individual one, see READMEs
in [gtest](gtest/README.md) and [rust](rust/README.md)

## Running

Please see following docs

- [gtest](gtest/README.md)
- [rust](rust/README.md)

## Testing

Please see following docs

- [gtest](gtest/README.md)
- [rust](rust/README.md)
