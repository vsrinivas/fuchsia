
A test helper that manages memory-backed blobfs instances.

## Usage

As this test helper is intended to be included within a test package, all tests
utilizing this helper will need a few extra blobs included in their test
package, access to some non-standard sandbox features, and an injected isolated
devmgr component.

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

In each `test_package` that utilizes this crate, add a dependency on the
manifest and add it to the extra set of manifests to include in the package. Add
the `blobfs-corrupt` binary if tests will want to corrupt blobs.

```
test_package("example-test-package") {
  extra = [ blobfs_manifest ]
  deps = [
    ":blobfs.manifest",
    "//src/storage/tools/blobfs-corrupt",
    ...
  ]
  binaries = [
    {
      name = "blobfs-corrupt"
    },
    ...
  ...
}
```

For the `tests` group, ensure the isolated devmgr package is included in the
build:

```
group("tests") {
  testonly = true
  public_deps = [
    ":example-test-package",
    "//src/lib/storage/ramdevice_client:ramdisk-isolated-devmgr",
    ...
  ]
}
```

### Component Manifest

In the component manifest tests that utilize this crate, add the isolated devmgr
service and access to the "fuchsia.process.Launcher" service:

```json
{
    "facets": {
        "fuchsia.test": {
            "injected-services": {
                "fuchsia.test.IsolatedDevmgr": "fuchsia-pkg://fuchsia.com/ramdisk-isolated-devmgr#meta/ramdisk-isolated-devmgr.cmx"
            }
        }
    },
    "sandbox": {
        "services": [
            "fuchsia.process.Launcher",
            "fuchsia.test.IsolatedDevmgr"
        ]
    }
}
```
