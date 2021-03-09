A test helper that creates a mock crash reporter service which responds
based on a caller-provided function.

## Usage

### BUILD.gn
Nothing special, other than including this crate:

```
  deps = [
    "//src/sys/pkg/testing/mock-crash-reporter:lib",
  ]
```

### In tests
See the tests in `src/lib.rs` for examples of using this crate in various scenarios,
including making file calls succeed, fail, or act on external data.