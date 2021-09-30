# Contributing Tests to CTS

This guide provides instructions on how to contribute a test to CTS.


## How to write a prebuilt CTS test

A CTS prebuilt test is a Fuchsia package containing a test component. These
tests run entirely on the target device and are shipped in CTS as prebuilt
package archives in FAR format.

### Prerequisites

* Your prebuilt test must be written in C, C++, or Rust.
* This guide assumes that you have already defined a GN target for your executable.
* This guide assumes the reader is familiar with Fuchsia [Packages],
[Components] and [Component Manifests].


### Step 1: Create a test directory

The structure of the `//sdk/cts` directory mirrors the structure of SDK
artifacts. Your test should go in the same directory as the interface under test
is found in an SDK. For example:

| Tests for... | Should go in... |
|--------------|-----------------|
| Host tools   | //sdk/cts/tests/tools |
| FIDL interfaces | //sdk/cts/tests/fidl |
| Libraries    | //sdk/cts/tests/pkg |

See existing tests under `//sdk/cts/tests` for examples.

### Step 2: Create your test component

Note: This section assumes familiarity with the concept of [Test Components].

In your test directory's `BUILD.gn` file, wrap your executable as a Fuchsia
component. CTS provides a special GN template for creating a component:

```
import("//sdk/cts/build/cts.gni")

cts_fuchsia_component("my_test_component") {
  testonly = true
  manifest = "meta/my_test_component.cml",
  deps = [ ":my_test_binary" ]
}
```

### Step 3: Create your test package

CTS also provides a special GN template for creating a test package:

```
cts_fuchsia_test_package("my_test") {
  package_name = "my_test"
  test_components = [ ":my_test_component" ]
}
```

### Step 4: Run the test

These instructions require you to open several terminal tabs.

#### Tab 1: Start the Fuchsia emulator

```
fx vdl start --headless
```

* `--headless` disables graphical output.
* See [Start the Fuchsia Emulator] for more info.


#### Tab 2: Start the Fuchsia package server

```
fx serve
```

#### Tab 3: Stream the device logs

This step is useful for debugging your test.

```
fx log
```

#### Tab 4: Run the test

```
TARGET="..." # Fill in your test's GN target label.
fx set core.x64 --with $TARGET
fx test $TARGET # Or: fx test my_test
```

* `-v` enables verbose output.

See the section about "Debugging tips" below.

### Step 5. Verify your test passes as part of the CTS release

Note: TODO(http://fxbug.dev/84175): Automate these steps and update this section.

This step involves building the CTS in the same way that our CI does when it is
released, then running those CTS tests in your local checkout. It is necessary
because upon release, we automatically rewrite each CTS test package's name to
deduplicate it from the same package's name at HEAD (the build does not allow
duplicate names) and we must verify that the test still passes after this
rewrite.

To do this follow these steps:

```
# Build the CTS.
fx set core.x64 --with //sdk/cts:cts_artifacts --args cts_version=\"canary\"
fx build

# Overwrite your checkout's copy of CTS.
# Optionally, backup the directory first or restore it later with `jiri update -gc`.
sudo rm -rf prebuilt/cts/canary/linux-amd64/cts
sudo cp -r out/default/cts prebuilt/cts/linux-amd64/cts

# Run the tests.
fx set core.64 --with //sdk/cts/canary:my_test_canary
fx test //sdk/cts/canary:my_test_canary
```

Some common causes of test failure at this stage include:

* The test launches one of its child components using an absolute component URL.
  * **Explanation**: The URL is no longer correct because the test's package was
  renamed.
  * **Solution**: Use a [relative component URL] instead.
* The test is missing a dependency.
  * **Explanation**: It's possible that the Fuchsia system contained a different
  set of packages when you built and ran your test at HEAD vs as part of the CTS
  and that your test depended on some of those packages.
  * **Solution**: Make sure your test's GN target explicitly lists all of its
  direct dependencies as `deps`.

If you need additional help debugging at this step, please reach out to
fuchsia-cts-team@google.com.

### Step 5. Make the test run on presubmit

This step causes the version of your test from Fuchsia's HEAD commit to run as
part of Fuchsia's presumbit queue. It *does not* include your test in the CTS
release (See the next section).

Add a "tests" group to your BUILD file:

```
group("tests") {
  testonly = true
  deps = [ ":my_test" ]
}
```

Next add this target as a dependency to the closest ancestor `group("tests")`
target.

### Step 6. Make the test run as part of the CTS release

This step includes your test in the CTS release, which guarantees that your test
cannot be broken between Fuchsia milestone releases (typically made every six
weeks).

Add a `sdk_molecule` target and use it to mark all of your test artifacts for
inclusion in CTS. Each `cts_*` template declares an `sdk_atom` or `sdk_molecule`
target with the name `${target_name}_sdk`. List of each those targets as deps:

```
sdk_molecule("test_sdks") {
  testonly = true
  deps = [
    ":my_test_binary_sdk", # If your binary was declared as a cts_* template
    ":my_test_component_sdk",
    ":my_test_sdk",
  ]
}
```

Next add this target as a dependency to the closest ancestor
`sdk_molecule("test_sdks")`.

Finally, add your test to `//sdk/cts/data/test_manifest.json`. The order of
the JSON objects in the file does not matter:

```
{
  "archive_name": "my_test",
  "component_name": "my_test_component.cm",
  "package": "my_test"
},
```

Once these steps are complete, submit your change and you should see your test run
as part of the next CTS release.

## How to write an API test

Note: TODO(fxbug.dev/84165)

## How to write a CTS test for a tool

Note: See fxbug.dev/83948; Tools tests are not yet supported in CTS.

## Debugging Tips

* If your test hangs, use `ffx component list -v` to inspect its current state.

[Components]: /docs/concepts/components/v2
[Component Manifests]: /docs/concepts/components/v2/component_manifests.md
[Start the Fuchsia Emulator]: /docs/get-started/set_up_femu.md
[Fuchsia language policy]: /docs/contribute/governance/policy/programming_languages.md
[Packages]: /docs/concepts/packages/package.md
[relative component URL]: /docs/concepts/components/component_urls.md
[Test Components]: /docs/concepts/testing/v2/test_component.md
