A test helper that creates a mock reboot service which responds based on a
caller-provided function.

## Usage

### BUILD.gn
Nothing special, other than including this crate:

```
  deps = [
    "//src/sys/pkg/testing/mock-reboot",
  ]
```

### In tests
See the tests in `src/lib.rs` for examples of using this crate in various scenarios,
including making reboot calls succeed, fail, or act on external data.