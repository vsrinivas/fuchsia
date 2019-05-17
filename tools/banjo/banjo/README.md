# Banjo

## About

TODO(surajmalhotra): Explain banjo's purpose and usage.

## Building

Run the following commands:

```
fx build tools/banjo/banjo
cp out/default/host_x64/banjo_bin zircon/prebuilt/downloads/banjo/banjo_bin
fx build
```

## Testing

Run the following commands:

```
fx set ${product}.${board} --with //bundles:tests
fx run-host-tests banjo_unittests_lib_test
```

## Releasing

Once you have submitted your changes and they have passed GI, runners in infra
will automatically compile and upload the prebuilt banjo binary to CIPD. In
order to introduce a new version of banjo into the build, perform the following
steps:

* Find linux and macos cipd packages from the following locations with matching
git revision hashes:

```
https://chrome-infra-packages.appspot.com/p/fuchsia/tools/banjo_bin/linux-amd64/+/
https://chrome-infra-packages.appspot.com/p/fuchsia/tools/banjo_bin/mac-amd64/+/
```

Note that the git hashes are only visible after clicking the instance ID links. The
instance IDs for mac and linux *will* be different. You may notice multiple git
revision hashes under a single instance ID; this occurs when multiple packages
uploaded to CIPD were binary identical so they got deduplicated.

* Update the following files with the git revision and Instance IDs:

```
//zircon/prebuilt/zircon.ensure
//zircon/prebuilt/zircon.versions
```

You can use
[this cl]( https://fuchsia.googlesource.com/fuchsia/+/0af3c7e6aee0f55d0da7ed9daa6b8e7b97291eda)
as a reference.

* Run `zircon/scripts/download-prebuilts && fx build` and ensure build works
correctly (confirm it has the new changes you expect).
* Upload change for review.
