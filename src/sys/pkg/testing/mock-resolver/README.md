A test helper that creates a mock resolver backed by a tempdir serving
package directories, without spinning up a pkgfs instance.

## Usage

### BUILD.gn
Nothing special, other than including this crate:

```
  deps = [
    "//src/sys/pkg/testing/mock-resolver",
  ]
```

### Component Manifest
In the component manifest for the tests that utilize this crate, add
the `isolated-temp` capability to allow this crate to create tempdirs.

```json
{
    "sandbox": {
        "features": [
            "isolated-temp"
        ]
    }
}
```