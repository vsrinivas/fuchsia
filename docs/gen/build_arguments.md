# GN Build Arguments

### always_zedboot
 Build boot images that prefer Zedboot over local boot.

**Default value:** false


### amber_keys_dir
 Directory containing signing keys used by amber-publish.

**Default value:** "//garnet/go/src/amber/keys"


### amber_repository_blobs_dir

**Default value for `target_cpu = "arm64"`:** "//out/arm64/amber-files/repository/blobs"

**Default value for `target_cpu = "x64"`:** "//out/x64/amber-files/repository/blobs"


### amber_repository_dir
 Directory containing files named by their merkleroot content IDs in
 ASCII hex.  The //build/image:amber_publish_blobs target populates
 this with copies of build products, but never removes old files.

**Default value for `target_cpu = "arm64"`:** "//out/arm64/amber-files"

**Default value for `target_cpu = "x64"`:** "//out/x64/amber-files"


### arm_float_abi
 The ARM floating point mode. This is either the string "hard", "soft", or
 "softfp". An empty string means to use the default one for the
 arm_version.

**Default value for `target_cpu = "arm64"`:** ""

No values for `target_cpu = "x64"`.

### arm_optionally_use_neon
 Whether to enable optional NEON code paths.

**Default value for `target_cpu = "arm64"`:** false

No values for `target_cpu = "x64"`.

### arm_tune
 The ARM variant-specific tuning mode. This will be a string like "armv6"
 or "cortex-a15". An empty string means to use the default for the
 arm_version.

**Default value for `target_cpu = "arm64"`:** ""

No values for `target_cpu = "x64"`.

### arm_use_neon
 Whether to use the neon FPU instruction set or not.

**Default value for `target_cpu = "arm64"`:** true

No values for `target_cpu = "x64"`.

### arm_version

**Default value for `target_cpu = "arm64"`:** 8

No values for `target_cpu = "x64"`.

### build_intel_gen

**Default value for `target_cpu = "arm64"`:** false

**Default value for `target_cpu = "x64"`:** true


### build_libvulkan_arm_mali
 This is a list of targets that will be built as drivers. If more than one
 target is given then use_vulkan_loader_for_tests must be set to true, as
 otherwise tests won't know which libvulkan to use. Example gn arg:
 build_libvulkan_arm_mali =
 ["//third_party/arm-mali-bifrost:libvulkan_arm"]

**Default value:** []


### build_msd_arm_mali

**Default value for `target_cpu = "arm64"`:** true

**Default value for `target_cpu = "x64"`:** false


### build_msd_vsl_gc

**Default value:** false


### clang_prefix

**Default value:** "../../buildtools/linux-x64/clang/bin"


### crashpad_dependencies
 Determines various flavors of build configuration, and which concrete
 targets to use for dependencies. Valid values are "standalone",
 "chromium", and "fuchsia". Defaulted to "fuchsia" because
 "is_fuchsia_tree" is set.

**Default value:** "fuchsia"


### current_cpu

**Default value:** ""


### current_os

**Default value:** ""


### data_image_size
 The size of the minfs data partition image to create. Normally this image
 is added to FVM, and can therefore expand as needed. It must be at least
 10mb (the default) in order to be succesfully initialized.

**Default value:** "10m"


### enable_gfx_subsystem

**Default value:** true


### enable_sketchy_subsystem

**Default value:** true


### enable_value_subsystem

**Default value:** false


### enable_views_subsystem

**Default value:** true


### expat_build_root

**Default value:** "//third_party/expat"


### extra_authorized_keys_file
 Additional SSH authorized_keys file to include in the build.
 For example:
   extra_authorized_keys_file=\"$HOME/.ssh/id_rsa.pub\"

**Default value:** ""


### extra_variants
 Additional variant toolchain configs to support.
 This is just added to `known_variants`, which see.

**Default value:** []


### ffmpeg_profile

**Default value for `target_cpu = "arm64"`:** "default"

**Default value for `target_cpu = "x64"`:** "max"


### fuchsia_packages
 List of packages (a GN list of strings).  If unset, guessed based
 on which layer is found in the //.jiri_manifest file.

**Default value:** [[]](https://fuchsia.googlesource.com/build/+/master/gn/packages.gni#8)

**Current value for `target_cpu = "arm64"`:** [["garnet/packages/buildbot"]](/arm64/args.gn#2)

**Current value for `target_cpu = "x64"`:** [["garnet/packages/buildbot"]](/x64/args.gn#2)


### fvm_image_size
 The size in bytes of the FVM partition image to create. Normally this is
 computed to be just large enough to fit the blob and data images. The
 default value is "", which means to size based on inputs. Specifying a size
 that is too small will result in build failure.

**Default value:** ""


### glm_build_root

**Default value:** "//third_party/glm"


### goma_dir
 Absolute directory containing the Goma source code.

**Default value:** "/home/swarming/goma"


### host_byteorder

**Default value:** "undefined"


### host_cpu

**Default value:** "x64"


### host_os

**Default value:** "linux"


### host_tools_dir
 This is the directory where host tools intended for manual use by
 developers get installed.  It's something a developer might put
 into their shell's $PATH.  Host tools that are just needed as part
 of the build do not get copied here.  This directory is only for
 things that are generally useful for testing or debugging or
 whatnot outside of the GN build itself.  These are only installed
 by an explicit install_host_tools() rule (see //build/host.gni).

**Default value for `target_cpu = "arm64"`:** "//out/arm64/tools"

**Default value for `target_cpu = "x64"`:** "//out/x64/tools"


### icu_use_data_file
 Tells icu to load an external data file rather than rely on the icudata
 being linked directly into the binary.

 This flag is a bit confusing. As of this writing, icu.gyp set the value to
 0 but common.gypi sets the value to 1 for most platforms (and the 1 takes
 precedence).

 TODO(GYP) We'll probably need to enhance this logic to set the value to
 true or false in similar circumstances.

**Default value:** true


### is_debug
 Debug build.

**Default value:** true


### kernel_cmdline_file
 File containing kernel command line arguments to roll into the
 bootdata image used for booting.

**Default value:** ""


### known_variants
 List of variants that will form the basis for variant toolchains.
 To make use of a variant, set `select_variant` (which see).

 Normally this is not set as a build argument, but it serves to
 document the available set of variants.  See also `universal_variants`.
 Only set this to remove all the default variants here.
 To add more, set `extra_variants` instead.

 Each element of the list is one variant, which is a scope defining:

   configs (optional)
       [list of labels] Each label names a config that will be
       automatically used by every target built in this variant.
       For each config ${label}, there must also be a target
       ${label}_deps, which each target built in this variant will
       automatically depend on.  The `variant()` template is the
       recommended way to define a config and its `_deps` target at
       the same time.

   remove_common_configs (optional)
   remove_shared_configs (optional)
       [list of labels] This list will be removed (with `-=`) from
       the `default_common_binary_configs` list (or the
       `default_shared_library_configs` list, respectively) after
       all other defaults (and this variant's configs) have been
       added.

   deps (optional)
       [list of labels] Added to the deps of every target linked in
       this variant (as well as the automatic "${label}_deps" for
       each label in configs).

   name (required if configs is omitted)
       [string] Name of the variant as used in `select_variant`
       elements' `variant` fields.  It's a good idea to make it
       something concise and meaningful when seen as e.g. part of a
       directory name under `$root_build_dir`.  If name is omitted,
       configs must be nonempty and the simple names (not the full
       label, just the part after all /s and :s) of these configs
       will be used in toolchain names (each prefixed by a "-"), so
       the list of config names forming each variant must be unique
       among the lists in `known_variants + extra_variants`.

   toolchain_args (optional)
       [scope] Each variable defined in this scope overrides a
       build argument in the toolchain context of this variant.

   host_only (optional)
   target_only (optional)
       [scope] This scope can contain any of the fields above.
       These values are used only for host or target, respectively.
       Any fields included here should not also be in the outer scope.


**Default value:** [{
  configs = ["//build/config/lto"]
}, {
  configs = ["//build/config/lto:thinlto"]
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
}, {
  configs = ["//build/config/sanitizers:asan", "//build/config/sanitizers:sancov"]
  host_only = {
  remove_shared_configs = ["//build/config:symbol_no_undefined"]
}
}, {
  name = "asan_no_detect_leaks"
  toolchain_args = {
  asan_default_options = "detect_leaks=0"
}
  configs = ["//build/config/sanitizers:asan"]
  host_only = {
  remove_shared_configs = ["//build/config:symbol_no_undefined"]
}
}]


### magma_build_root

**Default value:** "//garnet/lib/magma"


### magma_enable_developer_build
 Enable this to have the msd include a suite of tests and invoke them
 automatically when the driver starts.

**Default value:** false


### magma_enable_tracing
 Enable this to include fuchsia tracing capability

**Default value:** true


### magma_python_path

**Default value:** "/b/s/w/ir/kitchen-workdir/third_party/mako"


### mesa_build_root

**Default value for `target_cpu = "x64"`:** "//third_party/mesa"

No values for `target_cpu = "arm64"`.

### msd_arm_enable_all_cores
 Enable all 8 cores, which is faster but emits more heat.

**Default value for `target_cpu = "arm64"`:** true

No values for `target_cpu = "x64"`.

### msd_arm_enable_cache_coherency
 With this flag set the system tries to use cache coherent memory if the
 GPU supports it.

**Default value for `target_cpu = "arm64"`:** true

No values for `target_cpu = "x64"`.

### msd_intel_enable_mapping_cache

**Default value for `target_cpu = "x64"`:** false

No values for `target_cpu = "arm64"`.

### msd_intel_gen_build_root

**Default value:** "//garnet/drivers/gpu/msd-intel-gen"


### prebuilt_libvulkan_arm_path

**Default value:** ""


### rustc_prefix
 Sets a custom base directory for `rustc` and `cargo`.
 This can be used to test custom Rust toolchains.

**Default value:** "//buildtools/linux-x64/rust/bin"


### scene_manager_vulkan_swapchain
 0 - use normal swapchain
 1 - use vulkan swapchain, but wait for real display
 2 - use vulkan swapchain with fixed-size fake display

**Default value:** 0


### scudo_default_options
 Default [Scudo](https://llvm.org/docs/ScudoHardenedAllocator.html)
 options (before the `SCUDO_OPTIONS` environment variable is read at
 runtime).  *NOTE:* This affects only components using the `scudo`
 variant (see GN build argument `select_variant`), and does not affect
 anything when the `use_scudo` build flag is set instead.

**Default value:** ["abort_on_error=1", "QuarantineSizeKb=0", "ThreadLocalQuarantineSizeKb=0"]


### sdk_dirs
 The directories to search for parts of the SDK.

 By default, we search the public directories for the various layers.
 In the future, we'll search a pre-built SDK as well.

**Default value:** ["//garnet/public", "//peridot/public", "//topaz/public"]


### select_variant
 List of "selectors" to request variant builds of certain targets.
 Each selector specifies matching criteria and a chosen variant.
 The first selector in the list to match a given target determines
 which variant is used for that target.

 Each selector is either a string or a scope.  A shortcut selector is
 a string; it gets expanded to a full selector.  A full selector is a
 scope, described below.

 A string selector can match a name in `select_variant_shortcuts`,
 which see.  If it's not a specific shortcut listed there, then it
 can be the name of any variant described in `known_variants` and
 `universal_variants` (and combinations thereof).  A `selector`
 that's a simple variant name selects for every binary built in the
 target toolchain: `{ host=false variant=selector }`.

 If a string selector contains a slash, then it's `"shortcut/filename"`
 and selects only the binary in the target toolchain whose `output_name`
 matches `"filename"`, i.e. it adds `output_name=["filename"]` to each
 selector scope that the shortcut's name alone would yield.

 The scope that forms a full selector defines some of these:

     variant (required)
         [string or false] The variant that applies if this selector
         matches.  This can be false to choose no variant, or a
         string that names the variant.  See `known_variants` and
         `universal_variants`.

 The rest below are matching criteria.  All are optional.
 The selector matches if and only if all of its criteria match.
 If none of these is defined, then the selector always matches.

 The first selector in the list to match wins and then the rest of
 the list is ignored. So construct more complex rules by using a
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
         [strings]: "executable", "loadable_module", or "driver_module"

     output_name
         [strings]: target's `output_name` (default: its `target name`)

     label
         [strings]: target's full label with `:` (without toolchain suffix)

     name
         [strings]: target's simple name (label after last `/` or `:`)

     dir
         [strings]: target's label directory (`//dir` for `//dir:name`).

**Default value:** []


### select_variant_canonical
 *This should never be set as a build argument.*
 It exists only to be set in `toolchain_args`.
 See //build/toolchain/clang_toolchain.gni for details.

**Default value:** []


### select_variant_shortcuts
 List of short names for commonly-used variant selectors.  Normally this
 is not set as a build argument, but it serves to document the available
 set of short-cut names for variant selectors.  Each element of this list
 is a scope where `.name` is the short name and `.select_variant` is a
 a list that can be spliced into `select_variant`, which see.

**Default value:** [{
  select_variant = [{
  variant = "asan_no_detect_leaks"
  host = true
  dir = ["//third_party/yasm", "//third_party/vboot_reference", "//garnet/tools/vboot_reference"]
}, {
  variant = "asan"
  host = true
}]
  name = "host_asan"
}]


### synthesize_packages
 List of extra packages to synthesize on the fly.  This is only for
 things that do not appear normally in the source tree.  Synthesized
 packages can contain build artifacts only if they already exist in some
 part of the build.  They can contain arbitrary verbatim files.
 Synthesized packages can't express dependencies on other packages.

 Each element of this list is a scope that is very much like the body of
 a package() template invocation (see //build/package.gni).  That scope
 must set `name` to the string naming the package, as would be the name
 in the package() target written in a GN file.  This must be unique
 among all package names.

**Default value:** []


### system_package_key
 The package key to use for signing Fuchsia packages made by the
 `package()` template (and the `system_image` packge).  If this
 doesn't exist yet when it's needed, it will be generated.  New
 keys can be generated with the `pm -k FILE genkey` host command.

**Default value:** "//build/development.key"


### target_cpu

**Default value:** ""

**Current value for `target_cpu = "arm64"`:** ["arm64"](/arm64/args.gn#1)

**Current value for `target_cpu = "x64"`:** ["x64"](/x64/args.gn#1)


### target_os

**Default value:** ""


### target_sysroot
 The absolute path of the sysroot that is used with the target toolchain.

**Default value:** ""


### thinlto_cache_dir
 ThinLTO cache directory path.

**Default value for `target_cpu = "arm64"`:** "thinlto-cache"

**Default value for `target_cpu = "x64"`:** "host_x64/thinlto-cache"


### thinlto_jobs
 Number of parallel ThinLTO jobs.

**Default value:** 8


### toolchain_manifests
 Manifest files describing target libraries from toolchains.
 Can be either // source paths or absolute system paths.

**Default value for `target_cpu = "arm64"`:** ["/b/s/w/ir/kitchen-workdir/buildtools/linux-x64/clang/lib/aarch64-fuchsia.manifest"]

**Default value for `target_cpu = "x64"`:** ["/b/s/w/ir/kitchen-workdir/buildtools/linux-x64/clang/lib/x86_64-fuchsia.manifest"]


### toolchain_variant
 *This should never be set as a build argument.*
 It exists only to be set in `toolchain_args`.
 See //build/toolchain/clang_toolchain.gni for details.
 This variable is a scope giving details about the current toolchain:
     toolchain_variant.base
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
     toolchain_variant.name
         [string] The name of this variant, as used in `variant` fields in
         `select_variant` clauses.  In the base toolchain and its
         `shlib_toolchain`, this is "".
     toolchain_variant.suffix
         [string] This is "-${toolchain_variant.name}", "" if name is empty.
     toolchain_variant.is_pic_default
         [bool] This is true in `shlib_toolchain`.
 The other fields are the variant's effects as defined in `known_variants`.

**Default value for `target_cpu = "arm64"`:** {
  base = "//build/toolchain/fuchsia:arm64"
}

**Default value for `target_cpu = "x64"`:** {
  base = "//build/toolchain/fuchsia:x64"
}


### universal_variants

**Default value:** [{
  toolchain_args = {
  is_debug = false
}
  configs = []
  name = "release"
}]


### use_ccache
 Set to true to enable compiling with ccache

**Default value:** false


### use_goma
 Set to true to enable distributed compilation using Goma.

**Default value:** false


### use_lto
 Use link time optimization (LTO).

**Default value:** false


### use_mock_magma

**Default value for `target_cpu = "x64"`:** false

No values for `target_cpu = "arm64"`.

### use_scudo
 Enable the [Scudo](https://llvm.org/docs/ScudoHardenedAllocator.html)
 memory allocator.

**Default value:** false


### use_thinlto
 Use ThinLTO variant of LTO if use_lto = true.

**Default value:** true


### use_vulkan_loader_for_tests
 Mesa doesn't properly handle loader-less operation;
 their GetInstanceProcAddr implementation returns 0 for some interfaces.
 On ARM there may be multiple libvulkan_arms, so they can't all be linked
 to.

**Default value:** true


### vk_loader_debug

**Default value:** "warn,error"


### zedboot_cmdline_file
 File containing kernel command line arguments to roll into the
 bootdata image used for zedboot.

**Default value:** ""


### zircon_asserts

**Default value:** true


### zircon_aux_manifests
 Manifest files describing extra libraries from a Zircon build
 not included in `zircon_boot_manifests`, such as an ASan build.
 Can be either // source paths or absolute system paths.

 Since Zircon manifest files are relative to a Zircon source directory
 rather than to the directory containing the manifest, these are assumed
 to reside in a build directory that's a direct subdirectory of the
 Zircon source directory and thus their contents can be taken as
 relative to `get_path_info(entry, "dir") + "/.."`.
 TODO(mcgrathr): Make Zircon manifests self-relative too and then
 merge this and toolchain_manifests into generic aux_manifests.

**Default value for `target_cpu = "arm64"`:** ["//out/build-zircon/build-arm64-ulib/bootfs.manifest"]

**Default value for `target_cpu = "x64"`:** ["//out/build-zircon/build-x64-ulib/bootfs.manifest"]


### zircon_boot_groups
 Groups to include from the Zircon /boot manifest into /boot.
 This is either "all" or a comma-separated list of one or more of:
   core -- necessary to boot
   misc -- utilities in /bin
   test -- test binaries in /bin and /test

**Default value:** "core"


### zircon_boot_manifests
 Manifest files describing files to go into the `/boot` filesystem.
 Can be either // source paths or absolute system paths.
 `zircon_boot_groups` controls which files are actually selected.

 Since Zircon manifest files are relative to a Zircon source directory
 rather than to the directory containing the manifest, these are assumed
 to reside in a build directory that's a direct subdirectory of the
 Zircon source directory and thus their contents can be taken as
 relative to `get_path_info(entry, "dir") + "/.."`.

**Default value for `target_cpu = "arm64"`:** ["//out/build-zircon/build-arm64/bootfs.manifest"]

**Default value for `target_cpu = "x64"`:** ["//out/build-zircon/build-x64/bootfs.manifest"]


### zircon_build_dir
 Zircon build directory for `target_cpu`, containing `.manifest` and
 `.bin` files for Zircon's BOOTFS, BOOTDATA, and kernel image.

**Default value for `target_cpu = "arm64"`:** "//out/build-zircon/build-arm64"

**Default value for `target_cpu = "x64"`:** "//out/build-zircon/build-x64"


### zircon_build_root

**Default value:** "//zircon"


### zircon_system_groups
 TODO(mcgrathr): Could default to "" for !is_debug, or "production
 build".  Note including "test" here places all of Zircon's tests
 into /system/test, which means that Fuchsia bots run those tests
 too.

**Default value:** "misc,test"


### zircon_tools_dir
 Where to find Zircon's host-side tools that are run as part of the build.

**Default value:** "//out/build-zircon/tools"

