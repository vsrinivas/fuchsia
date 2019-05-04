# GN Build Arguments

## All builds

### dart_force_product
Forces all Dart and Flutter apps to build in a specific configuration that
we use to build products.

**Current value (from the default):** `false`

From [//topaz/runtime/dart/config.gni:10](https://fuchsia.googlesource.com/topaz/+/2befe07453e35b0b808a83897e687f9d7e0deb66/runtime/dart/config.gni#10)

### enable_input_subsystem

**Current value (from the default):** `true`

From //garnet/bin/ui/scenic/BUILD.gn:14

### magma_python_path

**Current value (from the default):** `"/b/s/w/ir/k/third_party/mako"`

From //garnet/lib/magma/gnbuild/magma.gni:14

### sdk_id
Identifier for the Core SDK.

**Current value (from the default):** `""`

From //sdk/config.gni:7

### skia_use_harfbuzz

**Current value (from the default):** `true`

From //third_party/skia/gn/skia.gni:14

### toolchain_variant
*This should never be set as a build argument.*
It exists only to be set in `toolchain_args`.
See //build/toolchain/clang_toolchain.gni for details.
This variable is a scope giving details about the current toolchain:
    `toolchain_variant.base`
        [label] The "base" toolchain for this variant, *often the
        right thing to use in comparisons, not `current_toolchain`.*
        This is the toolchain actually referenced directly in GN
        source code.  If the current toolchain is not
        `shlib_toolchain` or a variant toolchain, this is the same
        as `current_toolchain`.  In one of those derivative
        toolchains, this is the toolchain the GN code probably
        thought it was in.  This is the right thing to use in a test
        like `toolchain_variant.base == target_toolchain`, rather
        rather than comparing against `current_toolchain`.
    `toolchain_variant.name`
        [string] The name of this variant, as used in `variant` fields
        in [`select_variant`](#select_variant) clauses.  In the base
        toolchain and its `shlib_toolchain`, this is `""`.
    `toolchain_variant.suffix`
        [string] This is "-${toolchain_variant.name}", "" if name is empty.
    `toolchain_variant.is_pic_default`
        [bool] This is true in `shlib_toolchain`.
The other fields are the variant's effects as defined in
[`known_variants`](#known_variants).

**Current value (from the default):**
```
{
  base = "//build/toolchain/fuchsia:arm64"
}
```

From //build/config/BUILDCONFIG.gn:74

### vbmeta_b_partition

**Current value (from the default):** `""`

From //build/images/BUILD.gn:43

### blobfs_maximum_bytes
In addition to reserving space for inodes and data, fs needs additional
space for maintaining some internal data structures. So the
space required to reserve inodes and data may exceed sum of the space
needed for inodes and data.
maximum_bytes puts an upper bound on the total bytes reserved for inodes,
data bytes and reservation for all other internal fs metadata.
An empty string does not put any upper bound. A filesystem may
reserve few blocks required for its operations.

**Current value (from the default):** `""`

From //build/images/fvm.gni:47

### blobfs_minimum_data_bytes
Number of bytes to reserve for data in the fs. This is in addition
to what is reserved, if any, for the inodes. Data bytes constitutes
"usable" space of the fs.
An empty string does not reserve any additional space than minimum
required for the filesystem.

**Current value (from the default):** `""`

From //build/images/fvm.gni:36

### max_log_disk_usage
Controls how many bytes of space on disk are used to persist device logs.
Should be a string value that only contains digits.

**Current value (from the default):** `"0"`

From //garnet/bin/log_listener/BUILD.gn:13

### sdk_dirs
The directories to search for parts of the SDK.

By default, we search the public directories for the various layers.
In the future, we'll search a pre-built SDK as well.

**Current value (from the default):** `["//garnet/public", "//peridot/public", "//topaz/public"]`

From //build/config/fuchsia/sdk.gni:10

### skia_enable_particles

**Current value (from the default):** `true`

From //third_party/skia/modules/particles/BUILD.gn:7

### enable_frame_pointers
Controls whether the compiler emits full stack frames for function calls.
This reduces performance but increases the ability to generate good
stack traces, especially when we have bugs around unwind table generation.
It applies only for Fuchsia targets (see below where it is unset).

TODO(ZX-2361): Theoretically unwind tables should be good enough so we can
remove this option when the issues are addressed.

**Current value (from the default):** `true`

From //build/config/BUILD.gn:19

### expat_build_root

**Current value (from the default):** `"//third_party/expat"`

From //garnet/lib/magma/gnbuild/magma.gni:9

### framework_packages

**Current value (from the default):** `["collection", "flutter", "meta", "typed_data", "vector_math"]`

From [//topaz/runtime/flutter_runner/prebuilt_framework.gni:8](https://fuchsia.googlesource.com/topaz/+/2befe07453e35b0b808a83897e687f9d7e0deb66/runtime/flutter_runner/prebuilt_framework.gni#8)

### skia_qt_path

**Current value (from the default):** `""`

From //third_party/skia/BUILD.gn:49

### skia_use_libheif

**Current value (from the default):** `false`

From //third_party/skia/BUILD.gn:36

### dart_use_tcmalloc
Whether to link the standalone VM against tcmalloc. The standalone build of
the VM enables this only for Linux builds.

**Current value (from the default):** `false`

From //third_party/dart/runtime/runtime_args.gni:47

### select_variant_shortcuts
List of short names for commonly-used variant selectors.  Normally this
is not set as a build argument, but it serves to document the available
set of short-cut names for variant selectors.  Each element of this list
is a scope where `.name` is the short name and `.select_variant` is a
a list that can be spliced into [`select_variant`](#select_variant).

**Current value (from the default):**
```
[{
  name = "host_asan"
  select_variant = [{
  dir = ["//third_party/yasm", "//third_party/vboot_reference", "//garnet/tools/vboot_reference", "//third_party/shaderc/third_party/spirv-tools"]
  host = true
  variant = "asan_no_detect_leaks"
}, {
  host = true
  variant = "asan"
}]
}, {
  name = "asan"
  select_variant = [{
  target_type = ["driver_module"]
  variant = false
}, {
  host = false
  variant = "asan"
}]
}]
```

From //build/config/BUILDCONFIG.gn:449

### skia_enable_fontmgr_custom

**Current value (from the default):** `true`

From //third_party/skia/BUILD.gn:67

### skia_enable_vulkan_debug_layers

**Current value (from the default):** `false`

From //third_party/skia/BUILD.gn:48

### warn_on_sdk_changes
Whether to only warn when an SDK has been modified.
If false, any unacknowledged SDK change will cause a build failure.

**Current value (from the default):** `false`

From //build/sdk/config.gni:8

### concurrent_rust_jobs
Maximum number of Rust processes to run in parallel.

We run multiple rustc jobs in parallel, each of which can cause significant
amount of memory, especially when using LTO. To avoid out-of-memory errors
we explicitly reduce the number of jobs.

**Current value (from the default):** `14`

From //build/rust/BUILD.gn:15

### enable_gfx_subsystem

**Current value (from the default):** `true`

From //garnet/bin/ui/scenic/BUILD.gn:12

### log_startup_sleep

**Current value (from the default):** `"30000"`

From //garnet/bin/log_listener/BUILD.gn:14

### skia_enable_fontmgr_win

**Current value (from the default):** `false`

From //third_party/skia/BUILD.gn:71

### use_vulkan_loader_for_tests
Mesa doesn't properly handle loader-less operation;
their GetInstanceProcAddr implementation returns 0 for some interfaces.
On ARM there may be multiple libvulkan_arms, so they can't all be linked
to.

**Current value (from the default):** `true`

From //garnet/lib/magma/gnbuild/magma.gni:31

### auto_login_to_guest
Whether basemgr should automatically login as a persistent guest user.

**Current value (from the default):** `false`

From //peridot/bin/basemgr/BUILD.gn:13

### clang_lib_dir
Path to Clang lib directory.

**Current value (from the default):** `"../build/buildtools/linux-x64/clang/lib"`

From //build/images/manifest.gni:19

### dart_vm_code_coverage
Whether to enable code coverage for the standalone VM.

**Current value (from the default):** `false`

From //third_party/dart/runtime/runtime_args.gni:39

### shell_enable_vulkan

**Current value (from the default):** `false`

From //third_party/flutter/shell/config.gni:6

### skia_enable_nima

**Current value (from the default):** `false`

From //third_party/skia/BUILD.gn:44

### dart_aot_sharing_basis
module_suggester is not AOT compiled in debug builds

**Current value (from the default):** `""`

From [//topaz/runtime/dart/dart_component.gni:51](https://fuchsia.googlesource.com/topaz/+/2befe07453e35b0b808a83897e687f9d7e0deb66/runtime/dart/dart_component.gni#51)

### dart_target_arch
Explicitly set the target architecture to use a simulator.
Available options are: arm, arm64, x64, ia32, and dbc.

**Current value (from the default):** `"arm64"`

From //third_party/dart/runtime/runtime_args.gni:32

### skia_enable_fontmgr_fuchsia

**Current value (from the default):** `false`

From //third_party/skia/BUILD.gn:70

### skia_use_freetype

**Current value (from the default):** `true`

From //third_party/skia/BUILD.gn:25

### using_fuchsia_sdk
Only set in buildroots where targets configure themselves for use with the
Fuchsia SDK

**Current value (from the default):** `false`

From //build/fuchsia/sdk.gni:8

### dart_use_crashpad
Whether to link Crashpad library for crash handling. Only supported on
Windows for now.

**Current value (from the default):** `false`

From //third_party/dart/runtime/runtime_args.gni:51

### linux_guest_extras_path

**Current value (from the default):** `""`

From //garnet/bin/guest/pkg/linux_guest/BUILD.gn:13

### msd_arm_enable_protected_debug_swap_mode
In protected mode, faults don't return as much information so they're much harder to debug. To
work around that, add a mode where protected atoms are executed in non-protected mode and
vice-versa.

NOTE: The memory security ranges should also be set (in TrustZone) to the opposite of normal, so
that non-protected mode accesses can only access protected memory and vice versa.  Also,
growable memory faults won't work in this mode, so larger portions of growable memory should
precommitted (which is not done by default).

**Current value (from the default):** `false`

From //garnet/drivers/gpu/msd-arm-mali/src/BUILD.gn:23

### zircon_b_partition

**Current value (from the default):** `""`

From //build/images/BUILD.gn:40

### max_fvm_size
Maximum allowable size for the FVM in a release mode build
Zero means no limit

**Current value (from the default):** `"0"`

From //build/images/max_fvm_size.gni:8

### cloudkms_key_dir

**Current value (from the default):** `"projects/fuchsia-infra/locations/global/keyRings/test-secrets/cryptoKeys"`

From //build/testing/secret_spec.gni:8

### embedder_for_target
By default, the dynamic library target exposing the embedder API is only
built for the host. The reasoning is that platforms that have target
definitions would not need an embedder API because an embedder
implementation is already provided for said target. This flag allows tbe
builder to obtain a shared library exposing the embedder API for alternative
embedder implementations.

**Current value (from the default):** `false`

From //third_party/flutter/shell/platform/embedder/embedder.gni:12

### skia_enable_tools

**Current value (from the default):** `false`

From //third_party/skia/gn/skia.gni:12

### vbmeta_a_partition

**Current value (from the default):** `""`

From //build/images/BUILD.gn:42

### allow_layer_guesswork
Does nothing.

Will be removed after 30 April 2019.

**Current value (from the default):** `false`

From //BUILD.gn:39

### enable_netboot
Whether to build the netboot zbi by default.

You can still build //build/images:netboot explicitly even if enable_netboot is false.

**Current value for `target_cpu = "arm64"`:** `false`

From //products/core.gni:7

**Overridden from the default:** `false`

From //build/images/BUILD.gn:33

**Current value for `target_cpu = "x64"`:** `false`

From //products/core.gni:7

**Overridden from the default:** `false`

From //build/images/BUILD.gn:33

### flutter_space_dart
Whether experimental space dart mode is enabled for Flutter applications.

**Current value (from the default):** `false`

From [//topaz/runtime/dart/dart_component.gni:38](https://fuchsia.googlesource.com/topaz/+/2befe07453e35b0b808a83897e687f9d7e0deb66/runtime/dart/dart_component.gni#38)

### fuchsia_vulkan_sdk
Path to Fuchsia Vulkan SDK

**Current value (from the default):** `"//third_party/vulkan_loader_and_validation_layers"`

From //build/vulkan/config.gni:10

### linux_runner_netmask

**Current value (from the default):** `"255.255.255.0"`

From //garnet/bin/guest/pkg/biscotti_guest/linux_runner/BUILD.gn:19

### goma_dir
Absolute directory containing the Goma source code.

**Current value (from the default):** `"/home/swarming/goma"`

From //build/toolchain/goma.gni:12

### skia_use_angle

**Current value (from the default):** `false`

From //third_party/skia/BUILD.gn:20

### minfs_minimum_data_bytes

**Current value (from the default):** `""`

From //build/images/fvm.gni:37

### system_package_key
The package key to use for signing Fuchsia packages made by the
`package()` template (and the `system_image` package).  If this
doesn't exist yet when it's needed, it will be generated.  New
keys can be generated with the `pm -k FILE genkey` host command.

**Current value (from the default):** `"//build/development.key"`

From //build/package.gni:18

### auto_update_packages
Whether the component loader should automatically update packages.

**Current value (from the default):** `true`

From //garnet/bin/sysmgr/BUILD.gn:10

### host_cpu

**Current value (from the default):** `"x64"`

### minfs_minimum_inodes

**Current value (from the default):** `""`

From //build/images/fvm.gni:29

### skia_enable_skpicture

**Current value (from the default):** `true`

From //third_party/skia/BUILD.gn:47

### skia_llvm_lib

**Current value (from the default):** `"LLVM"`

From //third_party/skia/BUILD.gn:57

### cache_package_labels
If you add package labels to this variable, the packages will be included
in the 'cache' package set, which represents an additional set of software
that is made available on disk immediately after paving and in factory
flows. These packages are not updated with an OTA, but instead are updated
ephemerally. This cache of software can be evicted by the system if storage
pressure arises or other policies indicate.

**Current value for `target_cpu = "arm64"`:** `[]`

From //products/core.gni:24

**Overridden from the default:** `[]`

From //BUILD.gn:22

**Current value for `target_cpu = "x64"`:** `[]`

From //products/core.gni:24

**Overridden from the default:** `[]`

From //BUILD.gn:22

### crashpad_dependencies

**Current value (from the default):** `"fuchsia"`

From [//third_party/crashpad/build/crashpad_buildconfig.gni:22](https://chromium.googlesource.com/crashpad/crashpad/+/93366d782aabdfbb4e94242cf13f7d5cb77a5098/build/crashpad_buildconfig.gni#22)

### skia_generate_workarounds

**Current value (from the default):** `false`

From //third_party/skia/BUILD.gn:51

### skia_use_egl

**Current value (from the default):** `false`

From //third_party/skia/BUILD.gn:21

### skia_use_fontconfig

**Current value for `target_cpu = "arm64"`:** `false`

From //.gn:28

**Overridden from the default:** `true`

From //third_party/skia/BUILD.gn:23

**Current value for `target_cpu = "x64"`:** `false`

From //.gn:28

**Overridden from the default:** `true`

From //third_party/skia/BUILD.gn:23

### universal_variants

**Current value (from the default):**
```
[{
  configs = []
  name = "release"
  toolchain_args = {
  is_debug = false
}
}]
```

From //build/config/BUILDCONFIG.gn:423

### use_vboot
Use vboot images

**Current value (from the default):** `false`

From //build/images/boot.gni:11

### build_info_version
Logical version of the current build. If not set, defaults to the timestamp
of the most recent update.

**Current value (from the default):** `""`

From //garnet/BUILD.gn:28

### skia_lex

**Current value (from the default):** `false`

From //third_party/skia/BUILD.gn:52

### zedboot_cmdline_files
Files containing additional kernel command line arguments to bake into
the Zedboot image.  The contents of these files (in order) come after any
arguments directly in [`zedboot_cmdline_args`](#zedboot_cmdline_args).
These can be GN `//` source pathnames or absolute system pathnames.

**Current value (from the default):** `[]`

From //build/images/zedboot/BUILD.gn:23

### dart_lib_export_symbols
Whether libdart should export the symbols of the Dart API.

**Current value (from the default):** `true`

From //third_party/dart/runtime/runtime_args.gni:91

### blobfs_minimum_inodes
minimum_inodes is the number of inodes to reserve for the fs
An empty string does not reserve any additional space than minimum
required for the filesystem.

**Current value (from the default):** `""`

From //build/images/fvm.gni:28

### experimental_wlan_client_mlme
Selects the SoftMAC client implementation to use. Choices:
  false (default) - C++ Client MLME implementation
  true - Rust Client MLME implementation
This argument is temporary until Rust MLME is ready to be used.

**Current value (from the default):** `false`

From //src/connectivity/wlan/lib/mlme/cpp/BUILD.gn:10

### extra_authorized_keys_file
Additional SSH authorized_keys file to include in the build.
For example:
  extra_authorized_keys_file=\"$HOME/.ssh/id_rsa.pub\"

**Current value (from the default):** `""`

From [//third_party/openssh-portable/fuchsia/developer-keys/BUILD.gn:11](https://fuchsia.googlesource.com/third_party/openssh-portable/+/62f4ca0d82fa50a3405703837031a15c36dccdd4/fuchsia/developer-keys/BUILD.gn#11)

### flutter_runtime_mode
The runtime mode ("debug", "profile", "release", "dynamic_profile", or "dynamic_release")

**Current value (from the default):** `"debug"`

From //third_party/flutter/common/config.gni:15

### wlancfg_config_type
Selects the wlan configuration type to use. Choices:
  "client" - client mode
  "ap" - access point mode
  "" (empty string) - no configuration

**Current value (from the default):** `"client"`

From //src/connectivity/wlan/wlancfg/BUILD.gn:15

### fvm_slice_size
The size of the FVM partition images "slice size". The FVM slice size is a
minimum size of a particular chunk of a partition that is stored within
FVM. A very small slice size may lead to decreased throughput. A very large
slice size may lead to wasted space. The selected default size of 8mb is
selected for conservation of space, rather than performance.

**Current value (from the default):** `"8388608"`

From //build/images/fvm.gni:19

### go_vet_enabled
  go_vet_enabled
    [bool] if false, go vet invocations are disabled for all builds.

**Current value (from the default):** `false`

From //build/go/go_build.gni:20

### ledger_sync_credentials_file

**Current value (from the default):** `""`

From //src/ledger/bin/testing/sync_params.gni:6

### skia_enable_fontmgr_custom_empty

**Current value (from the default):** `false`

From //third_party/skia/BUILD.gn:68

### use_prebuilt_dart_sdk
Whether to use the prebuilt Dart SDK for everything.
When setting this to false, the preubilt Dart SDK will not be used in
situations where the version of the SDK matters, but may still be used as an
optimization where the version does not matter.

**Current value (from the default):** `true`

From //build/dart/dart.gni:15

### create_kernel_service_snapshot

**Current value (from the default):** `false`

From //third_party/dart/runtime/runtime_args.gni:101

### enable_value_subsystem

**Current value (from the default):** `false`

From //garnet/bin/ui/scenic/BUILD.gn:11

### skia_enable_flutter_defines

**Current value for `target_cpu = "arm64"`:** `true`

From //.gn:24

**Overridden from the default:** `false`

From //third_party/skia/BUILD.gn:16

**Current value for `target_cpu = "x64"`:** `true`

From //.gn:24

**Overridden from the default:** `false`

From //third_party/skia/BUILD.gn:16

### skia_use_fonthost_mac

**Current value (from the default):** `false`

From //third_party/skia/BUILD.gn:24

### base_package_labels
If you add package labels to this variable, the packages will be included in
the 'base' package set, which represents the set of packages that are part
of an OTA. These pacakages are updated as an atomic unit during an OTA
process and are immutable and are a superset of the TCB (Trusted Computing
Base) for a product. These packages are never evicted by the system.

**Current value for `target_cpu = "arm64"`:** `["//garnet/packages/config:kernel_crash_checker", "//garnet/packages/prod:crashpad_agent", "//garnet/packages/prod:feedback_agent", "//garnet/packages/prod:kernel_crash_checker", "//garnet/packages/products:base", "//topaz/bundles:buildbot"]`

From //root_build_dir/args.gn:3

**Overridden from the default:** `[]`

From //BUILD.gn:14

**Current value for `target_cpu = "x64"`:** `["//garnet/packages/config:kernel_crash_checker", "//garnet/packages/prod:crashpad_agent", "//garnet/packages/prod:feedback_agent", "//garnet/packages/prod:kernel_crash_checker", "//garnet/packages/products:base", "//topaz/bundles:buildbot"]`

From //root_build_dir/args.gn:3

**Overridden from the default:** `[]`

From //BUILD.gn:14

### board_packages

**Current value for `target_cpu = "arm64"`:** `[]`

From //boards/arm64.gni:7

**Overridden from the default:** `[]`

From //BUILD.gn:40

**Current value for `target_cpu = "x64"`:** `[]`

From //boards/x64.gni:9

**Overridden from the default:** `[]`

From //BUILD.gn:40

### dart_space_dart
Whether experimental space dart mode is enabled for Dart applications.

**Current value (from the default):** `false`

From [//topaz/runtime/dart/dart_component.gni:41](https://fuchsia.googlesource.com/topaz/+/2befe07453e35b0b808a83897e687f9d7e0deb66/runtime/dart/dart_component.gni#41)

### minfs_maximum_bytes

**Current value (from the default):** `""`

From //build/images/fvm.gni:48

### prebuilt_framework_path

**Current value (from the default):** `""`

From [//topaz/runtime/flutter_runner/prebuilt_framework.gni:6](https://fuchsia.googlesource.com/topaz/+/2befe07453e35b0b808a83897e687f9d7e0deb66/runtime/flutter_runner/prebuilt_framework.gni#6)

### full_dart_sdk

**Current value (from the default):** `false`

From //third_party/flutter/BUILD.gn:10

### skia_pdf_subset_harfbuzz
TODO: set skia_pdf_subset_harfbuzz to skia_use_harfbuzz.

**Current value (from the default):** `false`

From //third_party/skia/gn/skia.gni:18

### use_vbmeta
If true, then the paving script will pave vbmeta images to the target device.
It is assumed that the vbmeta image will be created by the custom_signing_script.

**Current value (from the default):** `false`

From //build/images/custom_signing.gni:16

### linux_runner_extras
If `true`, the extras.img will be built and mounted inside the container
at /mnt/chromeos.

This is useful for including some GN-built binaries into the guest image
without modifying the termina images.

**Current value (from the default):** `false`

From //garnet/bin/guest/pkg/biscotti_guest/linux_runner/BUILD.gn:26

### skia_use_icu

**Current value (from the default):** `true`

From //third_party/skia/gn/skia.gni:13

### known_variants
List of variants that will form the basis for variant toolchains.
To make use of a variant, set [`select_variant`](#select_variant).

Normally this is not set as a build argument, but it serves to
document the available set of variants.
See also [`universal_variants`](#universal_variants).
Only set this to remove all the default variants here.
To add more, set [`extra_variants`](#extra_variants) instead.

Each element of the list is one variant, which is a scope defining:

  `configs` (optional)
      [list of labels] Each label names a config that will be
      automatically used by every target built in this variant.
      For each config `${label}`, there must also be a target
      `${label}_deps`, which each target built in this variant will
      automatically depend on.  The `variant()` template is the
      recommended way to define a config and its `_deps` target at
      the same time.

  `remove_common_configs` (optional)
  `remove_shared_configs` (optional)
      [list of labels] This list will be removed (with `-=`) from
      the `default_common_binary_configs` list (or the
      `default_shared_library_configs` list, respectively) after
      all other defaults (and this variant's configs) have been
      added.

  `deps` (optional)
      [list of labels] Added to the deps of every target linked in
      this variant (as well as the automatic `${label}_deps` for
      each label in configs).

  `name` (required if configs is omitted)
      [string] Name of the variant as used in
      [`select_variant`](#select_variant) elements' `variant` fields.
      It's a good idea to make it something concise and meaningful when
      seen as e.g. part of a directory name under `$root_build_dir`.
      If name is omitted, configs must be nonempty and the simple names
      (not the full label, just the part after all `/`s and `:`s) of these
      configs will be used in toolchain names (each prefixed by a "-"),
      so the list of config names forming each variant must be unique
      among the lists in `known_variants + extra_variants`.

  `toolchain_args` (optional)
      [scope] Each variable defined in this scope overrides a
      build argument in the toolchain context of this variant.

  `host_only` (optional)
  `target_only` (optional)
      [scope] This scope can contain any of the fields above.
      These values are used only for host or target, respectively.
      Any fields included here should not also be in the outer scope.


**Current value (from the default):**
```
[{
  configs = ["//build/config/lto"]
}, {
  configs = ["//build/config/lto:thinlto"]
}, {
  configs = ["//build/config/profile"]
}, {
  configs = ["//build/config/scudo"]
}, {
  configs = ["//build/config/sanitizers:ubsan"]
}, {
  configs = ["//build/config/sanitizers:ubsan", "//build/config/sanitizers:sancov"]
}, {
  configs = ["//build/config/sanitizers:asan"]
  host_only = {
  remove_shared_configs = ["//build/config:symbol_no_undefined"]
}
  toolchain_args = {
  use_scudo = false
}
}, {
  configs = ["//build/config/sanitizers:asan", "//build/config/sanitizers:sancov"]
  host_only = {
  remove_shared_configs = ["//build/config:symbol_no_undefined"]
}
  toolchain_args = {
  use_scudo = false
}
}, {
  configs = ["//build/config/sanitizers:asan"]
  host_only = {
  remove_shared_configs = ["//build/config:symbol_no_undefined"]
}
  name = "asan_no_detect_leaks"
  toolchain_args = {
  asan_default_options = "detect_leaks=0"
  use_scudo = false
}
}, {
  configs = ["//build/config/sanitizers:asan", "//build/config/sanitizers:fuzzer"]
  host_only = {
  remove_shared_configs = ["//build/config:symbol_no_undefined"]
}
  remove_shared_configs = ["//build/config:symbol_no_undefined"]
  toolchain_args = {
  asan_default_options = "alloc_dealloc_mismatch=0"
  use_scudo = false
}
}, {
  configs = ["//build/config/sanitizers:ubsan", "//build/config/sanitizers:fuzzer"]
  remove_shared_configs = ["//build/config:symbol_no_undefined"]
}]
```

From //build/config/BUILDCONFIG.gn:338

### scenic_use_views2
Temporary flag, switches Flutter to using Scenic's new View API.

**Current value (from the default):** `false`

From //garnet/bin/ui/scenic/config.gni:7

### skia_enable_skshaper

**Current value (from the default):** `true`

From //third_party/skia/modules/skshaper/BUILD.gn:9

### universe_package_labels
If you add package labels to this variable, the packages will be included
in the 'universe' package set, which represents all software that is
produced that is to be published to a package repository or to the SDK by
the build. The build system ensures that the universe package set includes
the base and cache package sets, which means you do not need to redundantly
include those labels in this variable.

**Current value for `target_cpu = "arm64"`:** `["//garnet/packages/prod:vboot_reference", "//bundles:tools"]`

From //products/core.gni:26

**Overridden from the default:** `[]`

From //BUILD.gn:30

**Current value for `target_cpu = "x64"`:** `["//garnet/packages/prod:vboot_reference", "//bundles:tools"]`

From //products/core.gni:26

**Overridden from the default:** `[]`

From //BUILD.gn:30

### vbmeta_r_partition

**Current value (from the default):** `""`

From //build/images/BUILD.gn:44

### crash_diagnostics_dir
Clang crash reports directory path. Use empty path to disable altogether.

**Current value (from the default):** `"//root_build_dir/clang-crashreports"`

From //build/config/BUILD.gn:9

### data_partition_manifest
Path to manifest file containing data to place into the initial /data
partition.

**Current value (from the default):** `""`

From //build/images/BUILD.gn:28

### flutter_profile

**Current value (from the default):** `true`

From [//topaz/runtime/dart/dart_component.gni:32](https://fuchsia.googlesource.com/topaz/+/2befe07453e35b0b808a83897e687f9d7e0deb66/runtime/dart/dart_component.gni#32)

### skia_use_lua

**Current value (from the default):** `false`

From //third_party/skia/BUILD.gn:30

### skia_use_wuffs

**Current value (from the default):** `false`

From //third_party/skia/BUILD.gn:33

### skia_tools_require_resources

**Current value (from the default):** `false`

From //third_party/skia/BUILD.gn:59

### skia_use_zlib

**Current value (from the default):** `true`

From //third_party/skia/BUILD.gn:34

### dart_custom_version_for_pub
When this argument is a non-empty string, the version repoted by the
Dart VM will be one that is compatible with pub's interpretation of
semantic version strings. The version string will also include the values
of the argument. In particular the version string will read:

    "M.m.p-dev.x.x-$(dart_custom_version_for_pub)-$(short_git_hash)"

Where 'M', 'm', and 'p' are the major, minor and patch version numbers,
and 'dev.x.x' is the dev version tag most recently preceeding the current
revision. The short git hash can be omitted by setting
dart_version_git_info=false

**Current value (from the default):** `""`

From //third_party/dart/runtime/runtime_args.gni:73

### dart_snapshot_kind

**Current value (from the default):** `"kernel"`

From //third_party/dart/utils/application_snapshot.gni:14

### fuchsia_sdk_root
Consumers of the Fuchsia SDK instantiate templates for various SDK parts at
a specific spot within their buildroots. The target name for the specific
part is then derived from the part name as specified in the meta.json
manifest. Different buildroot instantiate the SDK parts at different
locations and then set this variable. GN rules can then prefix this variable
name in SDK builds to the name of the SDK part. This flag is meaningless in
non-SDK buildroots.

**Current value (from the default):** `""`

From //build/fuchsia/sdk.gni:17

### fvm_image_size
The size in bytes of the FVM partition image to create. Normally this is
computed to be just large enough to fit the blob and data images. The
default value is "", which means to size based on inputs. Specifying a size
that is too small will result in build failure.

**Current value (from the default):** `""`

From //build/images/fvm.gni:12

### have_secmem_ta
The secmem TA must be obtained elsewhere and put into the firmware
directory.

**Current value (from the default):** `false`

From //garnet/bin/sysmem-assistant/BUILD.gn:44

### zedboot_cmdline_args
List of kernel command line arguments to bake into the Zedboot image.
See //zircon/docs/kernel_cmdline.md and
[`zedboot_devmgr_config`](#zedboot_devmgr_config).

**Current value (from the default):** `[]`

From //build/images/zedboot/BUILD.gn:17

### bootfs_extra
List of extra manifest entries for files to add to the BOOTFS.
Each entry can be a "TARGET=SOURCE" string, or it can be a scope
with `sources` and `outputs` in the style of a copy() target:
`outputs[0]` is used as `TARGET` (see `gn help source_expansion`).

**Current value (from the default):** `[]`

From //build/images/BUILD.gn:491

### dart_debug
Instead of using is_debug, we introduce a different flag for specifying a
Debug build of Dart so that clients can still use a Release build of Dart
while themselves doing a Debug build.

**Current value (from the default):** `false`

From //third_party/dart/runtime/runtime_args.gni:9

### fuchsia_use_vulkan
Consolidated build toggle for use of Vulkan across Fuchsia

**Current value (from the default):** `true`

From //build/vulkan/config.gni:7

### linux_runner_gateway

**Current value (from the default):** `"10.0.0.1"`

From //garnet/bin/guest/pkg/biscotti_guest/linux_runner/BUILD.gn:18

### thinlto_jobs
Number of parallel ThinLTO jobs.

**Current value (from the default):** `8`

From //build/config/lto/config.gni:13

### skia_use_vulkan

**Current value (from the default):** `false`

From //third_party/skia/BUILD.gn:87

### update_kernels
(deprecated) List of kernel images to include in the update (OTA) package.
If no list is provided, all built kernels are included. The names in the
list are strings that must match the filename to be included in the update
package.

**Current value (from the default):** `[]`

From //build/images/BUILD.gn:497

### build_sdk_archives
Whether to build SDK tarballs.

**Current value (from the default):** `false`

From //build/sdk/sdk.gni:13

### dart_version_git_info
Whether the Dart binary version string should include the git hash and
git commit time.

**Current value (from the default):** `true`

From //third_party/dart/runtime/runtime_args.gni:60

### glm_build_root

**Current value (from the default):** `"//third_party/glm"`

From //garnet/lib/magma/gnbuild/magma.gni:11

### host_byteorder

**Current value (from the default):** `"undefined"`

From //build/config/host_byteorder.gni:7

### host_os

**Current value (from the default):** `"linux"`

### build_libvulkan_qcom_adreno
This is a list of targets that will be built as verisilicon vulkan ICDS.

**Current value (from the default):** `[]`

From //garnet/lib/magma/gnbuild/magma.gni:44

### custom_signing_script
If non-empty, the given script will be invoked to produce a signed ZBI
image. The given script must accept -z for the input zbi path, and -o for
the output signed zbi path. The path must be in GN-label syntax (i.e.
starts with //).

**Current value (from the default):** `""`

From //build/images/custom_signing.gni:12

### skia_use_x11

**Current value for `target_cpu = "arm64"`:** `false`

From //.gn:31

**Overridden from the default:** `true`

From //third_party/skia/BUILD.gn:37

**Current value for `target_cpu = "x64"`:** `false`

From //.gn:31

**Overridden from the default:** `true`

From //third_party/skia/BUILD.gn:37

### board_package_labels
A list of package labels to include in the 'base' package set. Used by the
board definition rather than the product definition.

**Current value for `target_cpu = "arm64"`:** `["//garnet/packages/prod:drivers", "//garnet/packages/prod:sysmem-assistant"]`

From //boards/arm64.gni:9

**Overridden from the default:** `[]`

From //BUILD.gn:34

**Current value for `target_cpu = "x64"`:** `["//garnet/packages/prod:drivers"]`

From //boards/x64.gni:13

**Overridden from the default:** `[]`

From //BUILD.gn:34

### linux_runner_ip
Default values for the guest network configuration.

These are currently hard-coded to match what is setup in the virtio-net
device.

See //garnet/bin/guest/vmm/device/virtio_net.cc for more details.

**Current value (from the default):** `"10.0.0.2"`

From //garnet/bin/guest/pkg/biscotti_guest/linux_runner/BUILD.gn:17

### persist_logs

**Current value (from the default):** `true`

From //build/persist_logs.gni:13

### skia_enable_fontmgr_win_gdi

**Current value (from the default):** `false`

From //third_party/skia/BUILD.gn:72

### use_mock_magma

**Current value (from the default):** `false`

From [//third_party/mesa/src/intel/vulkan/BUILD.gn:25](https://fuchsia.googlesource.com/third_party/mesa/+/232bf12b10899d335a3fe69d947a738541ace7d3/src/intel/vulkan/BUILD.gn#25)

### select_variant_canonical
*This should never be set as a build argument.*
It exists only to be set in `toolchain_args`.
See //build/toolchain/clang_toolchain.gni for details.

**Current value (from the default):** `[]`

From //build/config/BUILDCONFIG.gn:631

### skia_android_serial

**Current value (from the default):** `""`

From //third_party/skia/BUILD.gn:40

### skia_use_xps

**Current value (from the default):** `true`

From //third_party/skia/BUILD.gn:38

### rust_warnings
Sets whether Rust compiler warnings cause errors or not.
"deny" will make all warnings into errors, while "allow" will ignore warnings

**Current value (from the default):** `"deny"`

From //build/rust/config.gni:27

### skia_enable_gpu

**Current value (from the default):** `true`

From //third_party/skia/gn/skia.gni:11

### skia_enable_spirv_validation

**Current value (from the default):** `false`

From //third_party/skia/BUILD.gn:46

### concurrent_dart_jobs
Maximum number of Dart processes to run in parallel.

Dart analyzer uses a lot of memory which may cause issues when building
with many parallel jobs e.g. when using goma. To avoid out-of-memory
errors we explicitly reduce the number of jobs.

**Current value (from the default):** `16`

From //build/dart/BUILD.gn:15

### dart_component_kind

**Current value (from the default):** `"static_library"`

From //third_party/dart/runtime/runtime_args.gni:80

### enable_sketchy_subsystem

**Current value (from the default):** `false`

From //garnet/bin/ui/scenic/BUILD.gn:13

### zxcrypt_key_source
This argument specifies from where the system should obtain the zxcrypt
master key to the system data partition.

This value be reified as /boot/config/zxcrypt in both the zircon boot image
and the zedboot boot image, for consumption by fshost and the paver,
respectively.

Acceptable values are:
* "null": the device should use an all-0's master key, as we lack support
for any secure on-device storage.
* "tee": the device is required to have a Trusted Execution Environment
(TEE) which includes the "keysafe" Trusted Application (associated with the
KMS service).  The zxcrypt master key should be derived from a per-device
key accessible only to trusted apps running in the TEE.
* "tee-opportunistic": the device will attempt to use keys from the TEE if
available, but will fall back to using the null key if the key from the TEE
does not work, or if the TEE is not functional on this device.
* "tee-transitional": the device will require the use of a key from the TEE
for new volume creation, but will continue to try both a TEE-sourced key and
the null key when unsealing volumes.

In the future, we may consider adding support for TPMs, or additional logic
to explicitly support other fallback behavior.

**Current value (from the default):** `"null"`

From //build/images/zxcrypt.gni:29

### skia_llvm_path

**Current value (from the default):** `""`

From //third_party/skia/BUILD.gn:56

### dart_version

**Current value (from the default):** `""`

From //third_party/flutter/shell/version/version.gni:10

### debian_guest_earlycon

**Current value (from the default):** `false`

From //garnet/bin/guest/pkg/debian_guest/BUILD.gn:10

### extra_manifest_args
Extra args to globally apply to the manifest generation script.

**Current value (from the default):** `[]`

From //build/images/manifest.gni:22

### msd_intel_gen_build_root

**Current value (from the default):** `"//garnet/drivers/gpu/msd-intel-gen"`

From //garnet/lib/magma/gnbuild/magma.gni:10

### skia_gl_standard

**Current value (from the default):** `""`

From //third_party/skia/BUILD.gn:79

### dart_platform_bytecode
Whether the VM's platform dill file contains bytecode.

**Current value (from the default):** `false`

From //third_party/dart/runtime/runtime_args.gni:84

### skia_use_sfntly

**Current value for `target_cpu = "arm64"`:** `false`

From //.gn:30

**Overridden from the default:** `true`

From //third_party/skia/BUILD.gn:63

**Current value for `target_cpu = "x64"`:** `false`

From //.gn:30

**Overridden from the default:** `true`

From //third_party/skia/BUILD.gn:63

### toolchain_manifests
Manifest files describing target libraries from toolchains.
Can be either // source paths or absolute system paths.

**Current value (from the default):** `["/b/s/w/ir/k/buildtools/linux-x64/clang/lib/aarch64-fuchsia.manifest"]`

From //build/images/manifest.gni:11

### board_name
Board name used for paving and amber updates.

**Current value (from the default):** `""`

From //build/package.gni:21

### build_info_product
Product configuration of the current build

**Current value (from the default):** `""`

From //garnet/BUILD.gn:21

### rustc_prefix
Sets a custom base directory for `rustc` and `cargo`.
This can be used to test custom Rust toolchains.

**Current value (from the default):** `"//buildtools/linux-x64/rust/bin"`

From //build/rust/config.gni:17

### scenic_enable_vulkan_validation
Include the vulkan validation layers in scenic.

**Current value (from the default):** `true`

From //garnet/bin/ui/BUILD.gn:40

### skia_enable_pdf

**Current value for `target_cpu = "arm64"`:** `false`

From //.gn:25

**Overridden from the default:** `true`

From //third_party/skia/BUILD.gn:45

**Current value for `target_cpu = "x64"`:** `false`

From //.gn:25

**Overridden from the default:** `true`

From //third_party/skia/BUILD.gn:45

### select_variant
List of "selectors" to request variant builds of certain targets.
Each selector specifies matching criteria and a chosen variant.
The first selector in the list to match a given target determines
which variant is used for that target.

Each selector is either a string or a scope.  A shortcut selector is
a string; it gets expanded to a full selector.  A full selector is a
scope, described below.

A string selector can match a name in
[`select_variant_shortcuts`](#select_variant_shortcuts).  If it's not a
specific shortcut listed there, then it can be the name of any variant
described in [`known_variants`](#known_variants) and
[`universal_variants`](#universal_variants) (and combinations thereof).
A `selector` that's a simple variant name selects for every binary
built in the target toolchain: `{ host=false variant=selector }`.

If a string selector contains a slash, then it's `"shortcut/filename"`
and selects only the binary in the target toolchain whose `output_name`
matches `"filename"`, i.e. it adds `output_name=["filename"]` to each
selector scope that the shortcut's name alone would yield.

The scope that forms a full selector defines some of these:

    variant (required)
        [string or `false`] The variant that applies if this selector
        matches.  This can be `false` to choose no variant, or a string
        that names the variant.  See
        [`known_variants`](#known_variants) and
        [`universal_variants`](#universal_variants).

The rest below are matching criteria.  All are optional.
The selector matches if and only if all of its criteria match.
If none of these is defined, then the selector always matches.

The first selector in the list to match wins and then the rest of
the list is ignored.  So construct more complex rules by using a
"blacklist" selector with `variant=false` before a catch-all or
"whitelist" selector that names a variant.

Each "[strings]" criterion is a list of strings, and the criterion
is satisfied if any of the strings matches against the candidate string.

    host
        [boolean] If true, the selector matches in the host toolchain.
        If false, the selector matches in the target toolchain.

    testonly
        [boolean] If true, the selector matches targets with testonly=true.
        If false, the selector matches in targets without testonly=true.

    target_type
        [strings]: `"executable"`, `"loadable_module"`, or `"driver_module"`

    output_name
        [strings]: target's `output_name` (default: its `target name`)

    label
        [strings]: target's full label with `:` (without toolchain suffix)

    name
        [strings]: target's simple name (label after last `/` or `:`)

    dir
        [strings]: target's label directory (`//dir` for `//dir:name`).

**Current value (from the default):** `[]`

From //build/config/BUILDCONFIG.gn:626

### skia_compile_processors

**Current value (from the default):** `false`

From //third_party/skia/BUILD.gn:50

### zircon_build_root

**Current value (from the default):** `"//zircon"`

From //garnet/lib/magma/gnbuild/magma.gni:12

### always_zedboot
Build boot images that prefer Zedboot over local boot (only for EFI).

**Current value (from the default):** `false`

From //build/images/BUILD.gn:807

### dart_debug_optimization_level
The optimization level to use for debug builds. Defaults to 0 for builds with
code coverage enabled.

**Current value (from the default):** `"2"`

From //third_party/dart/runtime/runtime_args.gni:36

### dart_use_fallback_root_certificates
Whether to fall back to built-in root certificates when they cannot be
verified at the operating system level.

**Current value (from the default):** `false`

From //third_party/dart/runtime/runtime_args.gni:43

### enable_mdns_trace
Enables the tracing feature of mdns, which can be turned on using
"mdns-util verbose".

**Current value (from the default):** `false`

From //src/connectivity/network/mdns/service/BUILD.gn:15

### skia_use_dng_sdk

**Current value for `target_cpu = "arm64"`:** `false`

From //.gn:26

**Overridden from the default:** `true`

From //third_party/skia/BUILD.gn:62

**Current value for `target_cpu = "x64"`:** `false`

From //.gn:26

**Overridden from the default:** `true`

From //third_party/skia/BUILD.gn:62

### magma_enable_developer_build
Enable this to have the msd include a suite of tests and invoke them
automatically when the driver starts.

**Current value (from the default):** `false`

From //garnet/lib/magma/gnbuild/magma.gni:21

### add_qemu_to_build_archives
Whether to include images necessary to run Fuchsia in QEMU in build
archives.

**Current value (from the default):** `false`

From //build/images/BUILD.gn:50

### exclude_kernel_service
Whether the VM includes the kernel service in all modes (debug, release,
product).

**Current value (from the default):** `false`

From //third_party/dart/runtime/runtime_args.gni:88

### gocache_dir
  gocache_dir
    Directory GOCACHE environment variable will be set to. This directory
    will have build and test results cached, and is safe to be written to
    concurrently. If overridden, this directory must be a full path.

**Current value (from the default):** `"/b/s/w/ir/k/root_build_dir/host_x64/.gocache"`

From //build/go/go_build.gni:16

### skia_use_fixed_gamma_text

**Current value (from the default):** `false`

From //third_party/skia/BUILD.gn:26

### zircon_args
[Zircon GN build arguments](../../../zircon/docs/gen/build_arguments.md).
The default passes through GOMA settings and
[`select_variant`](#select_variant) shorthand selectors.
**Only set this if you want to wipe out all the defaults that
propagate from Fuchsia GN to Zircon GN.**  The default value
folds in [`zircon_extra_args`](#zircon_extra_args), so usually
it's better to just set `zircon_extra_args` and leave `zircon_args` alone.
Any individual Zircon build argument set in `zircon_extra_args` will
silently clobber the default value shown here.

**Current value (from the default):**
```
{
  default_deps = [":legacy-arm64"]
  enable_kernel_debugging_features = false
  goma_dir = "/home/swarming/goma"
  use_goma = false
  variants = []
}
```

From //build/config/fuchsia/zircon.gni:45

### active_partition

**Current value (from the default):** `""`

From //build/images/BUILD.gn:45

### escher_use_null_vulkan_config_on_host
Using Vulkan on host (i.e. Linux) is an involved affair that involves
downloading the Vulkan SDK, setting environment variables, and so forth...
all things that are difficult to achieve in a CQ environment.  Therefore,
by default we use a stub implementation of Vulkan which fails to create a
VkInstance.  This allows everything to build, and also allows running Escher
unit tests which don't require Vulkan.

**Current value (from the default):** `true`

From //garnet/public/lib/escher/BUILD.gn:16

### is_debug
Debug build.

**Current value (from the default):** `true`

From //build/config/BUILDCONFIG.gn:11

### scenic_ignore_vsync

**Current value (from the default):** `false`

From //garnet/lib/ui/gfx/BUILD.gn:8

### use_prebuilt_ffmpeg
Use a prebuilt ffmpeg binary rather than building it locally.  See
//src/media/lib/ffmpeg/README.md for details.  This is ignored when
building in variant builds for which there is no prebuilt.  In that
case, ffmpeg is always built from source so as to be built with the
selected variant's config.  When this is false (either explicitly or in
a variant build) then //third_party/ffmpeg must be in the source tree,
which requires:
`jiri import -name integration third_party/ffmpeg https://fuchsia.googlesource.com/integration`

**Current value (from the default):** `true`

From //src/media/lib/ffmpeg/BUILD.gn:14

### skia_enable_nvpr

**Current value (from the default):** `false`

From //third_party/skia/BUILD.gn:42

### skia_skqp_global_error_tolerance

**Current value (from the default):** `0`

From //third_party/skia/BUILD.gn:54

### target_os

**Current value (from the default):** `""`

### build_libvulkan_vsl_gc
This is a list of targets that will be built as verisilicon vulkan ICDS.

**Current value (from the default):** `[]`

From //garnet/lib/magma/gnbuild/magma.gni:41

### icu_use_data_file
Tells icu to load an external data file rather than rely on the icudata
being linked directly into the binary.

This flag is a bit confusing. As of this writing, icu.gyp set the value to
0 but common.gypi sets the value to 1 for most platforms (and the 1 takes
precedence).

TODO(GYP) We'll probably need to enhance this logic to set the value to
true or false in similar circumstances.

**Current value (from the default):** `true`

From [//third_party/icu/config.gni:15](https://fuchsia.googlesource.com/third_party/icu/+/65e4aa3b8cc6b41cf31136b9474a9c97bdc27739/config.gni#15)

### magma_enable_tracing
Enable this to include fuchsia tracing capability

**Current value (from the default):** `true`

From //garnet/lib/magma/gnbuild/magma.gni:17

### prebuilt_dart_sdk
Directory containing prebuilt Dart SDK.
This must have in its `bin/` subdirectory `gen_snapshot.OS-CPU` binaries.
Set to empty for a local build.

**Current value (from the default):** `"//topaz/tools/prebuilt-dart-sdk/linux-x64"`

From //build/dart/dart.gni:9

### skia_enable_fontmgr_empty

**Current value (from the default):** `false`

From //third_party/skia/BUILD.gn:65

### zircon_enable_kernel_debugging_features
Whether to include various features (non-shipping, insecure, etc.) in the
kernel build.

**Current value for `target_cpu = "arm64"`:** `false`

From //products/core.gni:9

**Overridden from the default:** `false`

From //build/config/fuchsia/zircon.gni:32

**Current value for `target_cpu = "x64"`:** `false`

From //products/core.gni:9

**Overridden from the default:** `false`

From //build/config/fuchsia/zircon.gni:32

### skia_use_libpng

**Current value (from the default):** `true`

From //third_party/skia/BUILD.gn:28

### use_lto
Use link time optimization (LTO).

**Current value (from the default):** `false`

From //build/config/lto/config.gni:7

### build_info_board
Board configuration of the current build

**Current value (from the default):** `""`

From //garnet/BUILD.gn:24

### concurrent_go_jobs
Maximum number of Go processes to run in parallel.

**Current value (from the default):** `16`

From //build/go/BUILD.gn:11

### concurrent_link_jobs
Maximum number of concurrent link jobs.

We often want to run fewer links at once than we do compiles, because
linking is memory-intensive. The default to use varies by platform and by
the amount of memory available, so we call out to a script to get the right
value.

**Current value (from the default):** `16`

From //build/toolchain/BUILD.gn:15

### dart_core_snapshot_kind
Controls the kind of core snapshot linked into the standalone VM. Using a
core-jit snapshot breaks the ability to change various flags that affect
code generation.

**Current value (from the default):** `"core"`

From //third_party/dart/runtime/runtime_args.gni:56

### export_x64_sdk_images

**Current value (from the default):** `false`

From //sdk/BUILD.gn:12

### skia_use_libjpeg_turbo

**Current value (from the default):** `true`

From //third_party/skia/BUILD.gn:27

### crashpad_use_boringssl_for_http_transport_socket
TODO(scottmg): https://crbug.com/crashpad/266 fuchsia:DX-690: BoringSSL
was removed from the Fuchsia SDK. Re-enable it when we have a way to acquire
a BoringSSL lib again.

**Current value (from the default):** `true`

From [//third_party/crashpad/util/net/tls.gni:21](https://chromium.googlesource.com/crashpad/crashpad/+/93366d782aabdfbb4e94242cf13f7d5cb77a5098/util/net/tls.gni#21)

### extra_variants
Additional variant toolchain configs to support.
This is just added to [`known_variants`](#known_variants).

**Current value (from the default):** `[]`

From //build/config/BUILDCONFIG.gn:403

### flutter_aot_sharing_basis
When AOT compiling, an app will reference objects in the sharing basis's
snapshot when available instead of writing the objects in its own snapshot.
The snapshot of the sharing basis app will be included in every other app's
package and deduplicated by blobfs.

**Current value (from the default):** `""`

From [//topaz/runtime/dart/dart_component.gni:27](https://fuchsia.googlesource.com/topaz/+/2befe07453e35b0b808a83897e687f9d7e0deb66/runtime/dart/dart_component.gni#27)

### flutter_default_app

**Current value (from the default):** `"flutter_jit_app"`

From [//topaz/runtime/dart/dart_component.gni:12](https://fuchsia.googlesource.com/topaz/+/2befe07453e35b0b808a83897e687f9d7e0deb66/runtime/dart/dart_component.gni#12)

### skia_enable_skottie

**Current value (from the default):** `true`

From //third_party/skia/modules/skottie/BUILD.gn:9

### dart_runtime_mode
Set the runtime mode. This affects how the runtime is built and what
features it has. Valid values are:
'develop' (the default) - VM is built to run as a JIT with all development
features enabled.
'profile' - The VM is built to run with AOT compiled code with only the
CPU profiling features enabled.
'release' - The VM is built to run with AOT compiled code with no developer
features enabled.

These settings are only used for Flutter, at the moment. A standalone build
of the Dart VM should leave this set to "develop", and should set
'is_debug', 'is_release', or 'is_product'.

TODO(rmacnak): dart_runtime_mode no longer selects whether libdart is build
for JIT or AOT, since libdart waw split into libdart_jit and
libdart_precompiled_runtime. We should remove this flag and just set
dart_debug/dart_product.

**Current value (from the default):** `"develop"`

From //third_party/dart/runtime/runtime_args.gni:28

### msd_arm_enable_all_cores
Enable all 8 cores, which is faster but emits more heat.

**Current value (from the default):** `true`

From //garnet/drivers/gpu/msd-arm-mali/src/BUILD.gn:9

### rust_lto
Sets the default LTO type for rustc bulids.

**Current value (from the default):** `"unset"`

From //build/rust/config.gni:20

### skia_enable_atlas_text

**Current value (from the default):** `false`

From //third_party/skia/BUILD.gn:64

### current_os

**Current value (from the default):** `""`

### use_scudo
TODO(davemoore): Remove this entire mechanism once standalone scudo is the
default (DNO-442)
Enable the [Scudo](https://llvm.org/docs/ScudoHardenedAllocator.html)
memory allocator.

**Current value (from the default):** `false`

From //build/config/scudo/scudo.gni:10

### vk_loader_debug

**Current value (from the default):** `"warn,error"`

From [//third_party/vulkan_loader_and_validation_layers/loader/BUILD.gn:26](https://fuchsia.googlesource.com/third_party/vulkan_loader_and_validation_layers/+/62e58ed31dc9711381fbe80817a89b51e9ff0686/loader/BUILD.gn#26)

### zircon_extra_args
[Zircon GN build arguments](../../../zircon/docs/gen/build_arguments.md).
This is included in the default value of [`zircon_args`](#zircon_args) so
you can set this to add things there without wiping out the defaults.
When you set `zircon_args` directly, then this has no effect at all.
Arguments you set here override any arguments in the default
`zircon_args`.  There is no way to append to a value from the defaults.
Note that for just setting simple (string-only) values in Zircon GN's
[`variants`](../../../zircon/docs/gen/build_arguments.md#variants), the
default [`zircon_args`](#zircon_args) uses a `variants` value derived from
[`select_variant`](#select_variant) so for simple cases there is no need
to explicitly set Zircon's `variants` here.

**Current value (from the default):** `{ }`

From //build/config/fuchsia/zircon.gni:27

### build_libvulkan_arm_mali
This is a list of targets that will be built as mali vulkan ICDS.

**Current value (from the default):** `[]`

From //garnet/lib/magma/gnbuild/magma.gni:38

### skia_enable_discrete_gpu

**Current value (from the default):** `true`

From //third_party/skia/BUILD.gn:43

### skia_use_expat

**Current value for `target_cpu = "arm64"`:** `false`

From //.gn:27

**Overridden from the default:** `true`

From //third_party/skia/BUILD.gn:22

**Current value for `target_cpu = "x64"`:** `false`

From //.gn:27

**Overridden from the default:** `true`

From //third_party/skia/BUILD.gn:22

### target_cpu

**Current value for `target_cpu = "arm64"`:** `"arm64"`

From //boards/arm64.gni:5

**Overridden from the default:** `""`

**Current value for `target_cpu = "x64"`:** `"x64"`

From //boards/x64.gni:5

**Overridden from the default:** `""`

### use_thinlto
Use ThinLTO variant of LTO if use_lto = true.

**Current value (from the default):** `true`

From //build/config/lto/config.gni:10

### current_cpu

**Current value (from the default):** `""`

### enable_dart_analysis
Enable all dart analysis

**Current value (from the default):** `true`

From //build/dart/dart_library.gni:15

### zedboot_devmgr_config
List of arguments to populate /boot/config/devmgr in the Zedboot image.

**Current value (from the default):** `["netsvc.netboot=true"]`

From //build/images/zedboot/BUILD.gn:26

### devmgr_config
List of arguments to add to /boot/config/devmgr.
These come after synthesized arguments to configure blobfs and pkgfs.

**Current value (from the default):** `[]`

From //build/images/BUILD.gn:474

### magma_build_root

**Current value (from the default):** `"//garnet/lib/magma"`

From //garnet/lib/magma/gnbuild/magma.gni:8

### skia_enable_fontmgr_android

**Current value (from the default):** `false`

From //third_party/skia/BUILD.gn:69

### skia_use_piex

**Current value (from the default):** `true`

From //third_party/skia/BUILD.gn:32

### symbol_level
How many symbols to include in the build. This affects the performance of
the build since the symbols are large and dealing with them is slow.
  2 means regular build with symbols.
  1 means minimal symbols, usually enough for backtraces only. Symbols with
internal linkage (static functions or those in anonymous namespaces) may not
appear when using this level.
  0 means no symbols.

**Current value (from the default):** `2`

From //build/config/compiler.gni:13

### debian_guest_qcow
Package the rootfs as a QCOW image (as opposed to a flat file).

**Current value (from the default):** `true`

From //garnet/bin/guest/pkg/debian_guest/BUILD.gn:9

### skia_enable_ccpr

**Current value (from the default):** `true`

From //third_party/skia/BUILD.gn:41

### skia_use_metal

**Current value (from the default):** `false`

From //third_party/skia/BUILD.gn:35

### skia_version

**Current value (from the default):** `""`

From //third_party/flutter/shell/version/version.gni:8

### target_sysroot
The absolute path of the sysroot that is used with the target toolchain.

**Current value (from the default):** `""`

From //build/config/sysroot.gni:7

### thinlto_cache_dir
ThinLTO cache directory path.

**Current value (from the default):** `"linux_x64/thinlto-cache"`

From //build/config/lto/config.gni:16

### dart_default_app
Controls whether dart_app() targets generate JIT or AOT Dart snapshots.
This defaults to JIT, use `fx set <ARCH> --args
'dart_default_app="dart_aot_app"' to switch to AOT.

**Current value (from the default):** `"dart_jit_app"`

From [//topaz/runtime/dart/dart_component.gni:19](https://fuchsia.googlesource.com/topaz/+/2befe07453e35b0b808a83897e687f9d7e0deb66/runtime/dart/dart_component.gni#19)

### kernel_cmdline_args
List of kernel command line arguments to bake into the boot image.
See also //zircon/docs/kernel_cmdline.md and
[`devmgr_config`](#devmgr_config).

**Current value (from the default):** `[]`

From //build/images/BUILD.gn:479

### msd_arm_enable_cache_coherency
With this flag set the system tries to use cache coherent memory if the
GPU supports it.

**Current value (from the default):** `true`

From //garnet/drivers/gpu/msd-arm-mali/src/BUILD.gn:13

### scudo_default_options
Default [Scudo](https://llvm.org/docs/ScudoHardenedAllocator.html)
options (before the `SCUDO_OPTIONS` environment variable is read at
runtime).  *NOTE:* This affects only components using the `scudo`
variant (see GN build argument `select_variant`), and does not affect
anything when the `use_scudo` build flag is set instead.

**Current value (from the default):** `["abort_on_error=1", "QuarantineSizeKb=0", "ThreadLocalQuarantineSizeKb=0", "DeallocationTypeMismatch=false", "DeleteSizeMismatch=false", "allocator_may_return_null=true"]`

From //build/config/scudo/scudo.gni:17

### skia_use_libwebp

**Current value for `target_cpu = "arm64"`:** `false`

From //.gn:29

**Overridden from the default:** `true`

From //third_party/skia/BUILD.gn:29

**Current value for `target_cpu = "x64"`:** `false`

From //.gn:29

**Overridden from the default:** `true`

From //third_party/skia/BUILD.gn:29

### engine_version

**Current value (from the default):** `""`

From //third_party/flutter/shell/version/version.gni:6

### kernel_cmdline_files
Files containing additional kernel command line arguments to bake into
the boot image.  The contents of these files (in order) come after any
arguments directly in [`kernel_cmdline_args`](#kernel_cmdline_args).
These can be GN `//` source pathnames or absolute system pathnames.

**Current value (from the default):** `[]`

From //build/images/BUILD.gn:485

### meta_package_labels
A list of labels for meta packages to be included in the monolith.

**Current value for `target_cpu = "arm64"`:** `["//build/images:config-data", "//build/images:shell-commands", "//src/sys/component_index:component_index"]`

From //products/core.gni:11

**Overridden from the default:** `[]`

From //build/images/BUILD.gn:36

**Current value for `target_cpu = "x64"`:** `["//build/images:config-data", "//build/images:shell-commands", "//src/sys/component_index:component_index"]`

From //products/core.gni:11

**Overridden from the default:** `[]`

From //build/images/BUILD.gn:36

### use_ccache
Set to true to enable compiling with ccache

**Current value (from the default):** `false`

From //build/toolchain/ccache.gni:9

### use_goma
Set to true to enable distributed compilation using Goma.

**Current value (from the default):** `false`

From //build/toolchain/goma.gni:9

### clang_prefix
The default clang toolchain provided by the buildtools. This variable is
additionally consumed by the Go toolchain.

**Current value (from the default):** `"../buildtools/linux-x64/clang/bin"`

From //build/config/clang/clang.gni:11

### skia_use_opencl

**Current value (from the default):** `false`

From //third_party/skia/BUILD.gn:31

### zircon_asserts

**Current value (from the default):** `true`

From //build/config/fuchsia/BUILD.gn:205

### rust_toolchain_triple_suffix
Sets the fuchsia toolchain target triple suffix (after arch)

**Current value (from the default):** `"fuchsia"`

From //build/rust/config.gni:23

### zircon_a_partition
arguments to fx flash script

**Current value (from the default):** `""`

From //build/images/BUILD.gn:39

### zircon_r_partition

**Current value (from the default):** `""`

From //build/images/BUILD.gn:41

### host_tools_dir
This is the directory where host tools intended for manual use by
developers get installed.  It's something a developer might put
into their shell's $PATH.  Host tools that are just needed as part
of the build do not get copied here.  This directory is only for
things that are generally useful for testing or debugging or
whatnot outside of the GN build itself.  These are only installed
by an explicit install_host_tools() rule (see //build/host.gni).

**Current value (from the default):** `"//root_build_dir/tools"`

From //build/host.gni:13

### prebuilt_framework_name

**Current value (from the default):** `""`

From [//topaz/runtime/flutter_runner/prebuilt_framework.gni:7](https://fuchsia.googlesource.com/topaz/+/2befe07453e35b0b808a83897e687f9d7e0deb66/runtime/flutter_runner/prebuilt_framework.gni#7)

### prebuilt_libvulkan_arm_path

**Current value (from the default):** `""`

From //garnet/lib/magma/gnbuild/magma.gni:23

### signed_image

**Current value (from the default):** `false`

From //build/images/BUILD.gn:46

## `target_cpu = "arm64"`

### amlogic_decoder_tests

**Current value (from the default):** `false`

From //garnet/drivers/video/amlogic-decoder/BUILD.gn:11

### arm_float_abi
The ARM floating point mode. This is either the string "hard", "soft", or
"softfp". An empty string means to use the default one for the
arm_version.

**Current value (from the default):** `""`

From //build/config/arm.gni:20

### arm_optionally_use_neon
Whether to enable optional NEON code paths.

**Current value (from the default):** `false`

From //build/config/arm.gni:31

### arm_tune
The ARM variant-specific tuning mode. This will be a string like "armv6"
or "cortex-a15". An empty string means to use the default for the
arm_version.

**Current value (from the default):** `""`

From //build/config/arm.gni:25

### arm_use_neon
Whether to use the neon FPU instruction set or not.

**Current value (from the default):** `true`

From //build/config/arm.gni:28

### arm_version

**Current value (from the default):** `8`

From //build/config/arm.gni:12

## `target_cpu = "x64"`

### msd_intel_enable_mapping_cache

**Current value (from the default):** `false`

From //garnet/drivers/gpu/msd-intel-gen/src/BUILD.gn:8

