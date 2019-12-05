
A test helper that manages memory-backed blobfs instances.

## Usage

As this test helper is intended to be included within a test package, all tests
utilizing this helper will need a few extra blobs included in their test package
and access to some non-standard sandbox features.

### BUILD.gn

Include the following template in all BUILD.gn files containing test packages
that utilize this crate.

```
# Include blobfs in the build, which comes from Zircon
generate_manifest("blobfs.manifest") {
  visibility = [ ":*" ]
  args = []
  foreach(pattern, [ "bin/blobfs" ]) {
    args += [ "--binary=" + pattern ]
  }
}
blobfs_manifest_outputs = get_target_outputs(":blobfs.manifest")
blobfs_manifest = blobfs_manifest_outputs[0]
```

Then, in each `test_package` that utilizes this crate, add a dependency on the
manifest and add it to the extra set of manifests to include in the package.

```
test_package("example-test-package") {
  extra = [ blobfs_manifest ]
  deps = [
    ":blobfs.manifest",
    ...
  ]
  ...
}
```

### Sandbox

In the sandbox section of tests that utilize this crate, add access to the
ramctl device and "fuchsia.process.Launcher" service:

```json
{
    "sandbox": {
        "dev": [
            "misc/ramctl"
        ],
        "services": [
            "fuchsia.process.Launcher"
        ]
    }
}
```
