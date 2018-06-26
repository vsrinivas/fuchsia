# GN Build Arguments

## All builds

### use_scudo
 Enable the [Scudo](https://llvm.org/docs/ScudoHardenedAllocator.html)
 memory allocator.

**Current value (from the default):** `false`

### vk_loader_debug

**Current value (from the default):** `"warn,error"`

### expat_build_root

**Current value (from the default):** `"//third_party/expat"`

### extra_variants
 Additional variant toolchain configs to support.
 This is just added to [`known_variants`](#known_variants).

**Current value (from the default):** `[]`

### host_os

**Current value (from the default):** `"linux"`

### sdk_dirs
 The directories to search for parts of the SDK.

 By default, we search the public directories for the various layers.
 In the future, we'll search a pre-built SDK as well.

**Current value (from the default):** `["//garnet/public", "//peridot/public", "//topaz/public"]`

### select_variant_shortcuts
 List of short names for commonly-used variant selectors.  Normally this
 is not set as a build argument, but it serves to document the available
 set of short-cut names for variant selectors.  Each element of this list
 is a scope where `.name` is the short name and `.select_variant` is a
 a list that can be spliced into [`select_variant`](#select_variant).

**Current value (from the default):** `[{
  select_variant = [{
  variant = "asan_no_detect_leaks"
  host = true
  dir = ["//third_party/yasm", "//third_party/vboot_reference", "//garnet/tools/vboot_reference"]
}, {
  variant = "asan"
  host = true
}]
  name = "host_asan"
}]`

### amber_repository_blobs_dir

**Current value (from the default):** `"//root_build_dir/amber-files/repository/blobs"`

### current_os

**Current value (from the default):** `""`

### rust_lto
 Sets the default LTO type for rustc bulids.

**Current value (from the default):** `"unset"`

### use_vulkan_loader_for_tests
 Mesa doesn't properly handle loader-less operation;
 their GetInstanceProcAddr implementation returns 0 for some interfaces.
 On ARM there may be multiple libvulkan_arms, so they can't all be linked
 to.

**Current value (from the default):** `true`

### host_tools_dir
 This is the directory where host tools intended for manual use by
 developers get installed.  It's something a developer might put
 into their shell's $PATH.  Host tools that are just needed as part
 of the build do not get copied here.  This directory is only for
 things that are generally useful for testing or debugging or
 whatnot outside of the GN build itself.  These are only installed
 by an explicit install_host_tools() rule (see [//build/host.gni](https://fuchsia.googlesource.com/build/+/master/host.gni)).

**Current value (from the default):** `"//root_build_dir/tools"`

### icu_use_data_file
 Tells icu to load an external data file rather than rely on the icudata
 being linked directly into the binary.

 This flag is a bit confusing. As of this writing, icu.gyp set the value to
 0 but common.gypi sets the value to 1 for most platforms (and the 1 takes
 precedence).

 TODO(GYP) We'll probably need to enhance this logic to set the value to
 true or false in similar circumstances.

**Current value (from the default):** `true`

### magma_build_root

**Current value (from the default):** `"//garnet/lib/magma"`

### use_thinlto
 Use ThinLTO variant of LTO if use_lto = true.

**Current value (from the default):** `true`

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


**Current value (from the default):** `[{
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
}]`

### magma_enable_developer_build
 Enable this to have the msd include a suite of tests and invoke them
 automatically when the driver starts.

**Current value (from the default):** `false`

### use_lto
 Use link time optimization (LTO).

**Current value (from the default):** `false`

### zircon_asserts

**Current value (from the default):** `true`

### amber_repository_dir
 Directory containing files named by their merkleroot content IDs in
 ASCII hex.  The [//build/image](https://fuchsia.googlesource.com/build/+/master/image):amber_publish_blobs target populates
 this with copies of build products, but never removes old files.

**Current value (from the default):** `"//root_build_dir/amber-files"`

### build_msd_arm_mali

**Current value (from the default):** `true`

### thinlto_cache_dir
 ThinLTO cache directory path.

**Current value (from the default):** `"host_x64/thinlto-cache"`

### build_libvulkan
 This is a list of targets that will be built as vulkan ICDS. If more than one
 target is given then use_vulkan_loader_for_tests must be set to true, as
 otherwise tests won't know which libvulkan to use.

**Current value (from the default):** `[]`

### enable_views_subsystem

**Current value (from the default):** `true`

### extra_authorized_keys_file
 Additional SSH authorized_keys file to include in the build.
 For example:
   extra_authorized_keys_file=\"$HOME/.ssh/id_rsa.pub\"

**Current value (from the default):** `""`

### fvm_image_size
 The size in bytes of the FVM partition image to create. Normally this is
 computed to be just large enough to fit the blob and data images. The
 default value is "", which means to size based on inputs. Specifying a size
 that is too small will result in build failure.

**Current value (from the default):** `""`

### host_cpu

**Current value (from the default):** `"x64"`

### toolchain_manifests
 Manifest files describing target libraries from toolchains.
 Can be either // source paths or absolute system paths.

**Current value (from the default):** `["/b/s/w/ir/kitchen-workdir/buildtools/linux-x64/clang/lib/aarch64-fuchsia.manifest"]`

### zircon_boot_manifests
 Manifest files describing files to go into the `/boot` filesystem.
 Can be either // source paths or absolute system paths.
 `zircon_boot_groups` controls which files are actually selected.

 Since Zircon manifest files are relative to a Zircon source directory
 rather than to the directory containing the manifest, these are assumed
 to reside in a build directory that's a direct subdirectory of the
 Zircon source directory and thus their contents can be taken as
 relative to `get_path_info(entry, "dir") + "/.."`.

**Current value (from the default):** `["//out/build-zircon/build-arm64/bootfs.manifest"]`

### always_zedboot
 Build boot images that prefer Zedboot over local boot.

**Current value (from the default):** `false`

### amber_keys_dir
 Directory containing signing keys used by amber-publish.

**Current value (from the default):** `"//garnet/go/src/amber/keys"`

### enable_sketchy_subsystem

**Current value (from the default):** `true`

### zircon_build_dir
 Zircon build directory for `target_cpu`, containing `.manifest` and
 `.bin` files for Zircon's BOOTFS, BOOTDATA, and kernel image.

**Current value (from the default):** `"//out/build-zircon/build-arm64"`

### target_os

**Current value (from the default):** `""`

### enable_gfx_subsystem

**Current value (from the default):** `true`

### msd_intel_gen_build_root

**Current value (from the default):** `"//garnet/drivers/gpu/msd-intel-gen"`

### rustc_prefix
 Sets a custom base directory for `rustc` and `cargo`.
 This can be used to test custom Rust toolchains.

**Current value (from the default):** `"//buildtools/linux-x64/rust/bin"`

### synthesize_packages
 List of extra packages to synthesize on the fly.  This is only for
 things that do not appear normally in the source tree.  Synthesized
 packages can contain build artifacts only if they already exist in some
 part of the build.  They can contain arbitrary verbatim files.
 Synthesized packages can't express dependencies on other packages.

 Each element of this list is a scope that is very much like the body of
 a package() template invocation (see [//build/package.gni](https://fuchsia.googlesource.com/build/+/master/package.gni)).  That scope
 must set `name` to the string naming the package, as would be the name
 in the package() target written in a GN file.  This must be unique
 among all package names.

**Current value (from the default):** `[]`

### zircon_boot_groups
 Groups to include from the Zircon /boot manifest into /boot.
 This is either "all" or a comma-separated list of one or more of:
   core -- necessary to boot
   misc -- utilities in /bin
   test -- test binaries in /bin and /test

**Current value (from the default):** `"core"`

### clang_prefix

**Current value (from the default):** `"../buildtools/linux-x64/clang/bin"`

### enable_value_subsystem

**Current value (from the default):** `false`

### kernel_cmdline_file
 File containing kernel command line arguments to roll into the
 bootdata image used for booting.

**Current value (from the default):** `""`

### select_variant_canonical
 *This should never be set as a build argument.*
 It exists only to be set in `toolchain_args`.
 See [//build/toolchain/clang_toolchain.gni](https://fuchsia.googlesource.com/build/+/master/toolchain/clang_toolchain.gni) for details.

**Current value (from the default):** `[]`

### system_package_key
 The package key to use for signing Fuchsia packages made by the
 `package()` template (and the `system_image` packge).  If this
 doesn't exist yet when it's needed, it will be generated.  New
 keys can be generated with the `pm -k FILE genkey` host command.

**Current value (from the default):** `"//build/development.key"`

### zircon_build_root

**Current value (from the default):** `"//zircon"`

### current_cpu

**Current value (from the default):** `""`

### enable_crashpad
 When this is set, Crashpad will be used to handle exceptions (which uploads
 crashes to the crash server), rather than crashanalyzer in Zircon (which
 prints a backtrace the the system log).

**Current value (from the default):** `false`

### goma_dir
 Absolute directory containing the Goma source code.

**Current value (from the default):** `"/home/swarming/goma"`

### build_vsl_gc

**Current value (from the default):** `true`

### universal_variants

**Current value (from the default):** `[{
  toolchain_args = {
  is_debug = false
}
  configs = []
  name = "release"
}]`

### use_boringssl_for_http_transport_socket

**Current value (from the default):** `true`

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

**Current value (from the default):** `["//out/build-zircon/build-arm64-ulib/bootfs.manifest"]`

### zircon_tools_dir
 Where to find Zircon's host-side tools that are run as part of the build.

**Current value (from the default):** `"//out/build-zircon/tools"`

### host_byteorder

**Current value (from the default):** `"undefined"`

### magma_python_path

**Current value (from the default):** `"/b/s/w/ir/kitchen-workdir/third_party/mako"`

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

### build_intel_gen

**Current value (from the default):** `false`

### scenic_vulkan_swapchain

**Current value (from the default):** `1`

### zircon_system_groups
 TODO(mcgrathr): Could default to "" for !is_debug, or "production
 build".  Note including "test" here places all of Zircon's tests
 into /system/test, which means that Fuchsia bots run those tests
 too.

**Current value (from the default):** `"misc,test"`

### fuchsia_packages
 List of packages (a GN list of strings).  If unset, guessed based
 on which layer is found in the //.jiri_manifest file.

**Current value for `target_cpu = "arm64"`:** `["garnet/packages/buildbot"]`
	From //root_build_dir/args.gn:2

**Overridden from the default:** `[]`
	From [//build/gn/packages.gni:8](https://fuchsia.googlesource.com/build/+/master/gn/packages.gni#8)

**Current value for `target_cpu = "x64"`:** `["garnet/packages/buildbot"]`
	From //root_build_dir/args.gn:2

**Overridden from the default:** `[]`
	From [//build/gn/packages.gni:8](https://fuchsia.googlesource.com/build/+/master/gn/packages.gni#8)

### glm_build_root

**Current value (from the default):** `"//third_party/glm"`

### target_sysroot
 The absolute path of the sysroot that is used with the target toolchain.

**Current value (from the default):** `""`

### thinlto_jobs
 Number of parallel ThinLTO jobs.

**Current value (from the default):** `8`

### toolchain_variant
 *This should never be set as a build argument.*
 It exists only to be set in `toolchain_args`.
 See [//build/toolchain/clang_toolchain.gni](https://fuchsia.googlesource.com/build/+/master/toolchain/clang_toolchain.gni) for details.
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

**Current value (from the default):** `{
  base = "//build/toolchain/fuchsia:arm64"
}`

### use_goma
 Set to true to enable distributed compilation using Goma.

**Current value (from the default):** `false`

### use_prebuilt_ffmpeg
 Use a prebuilt ffmpeg binary rather than building it locally.  See
 [//garnet/bin/media/media_player/ffmpeg/README.md](https://fuchsia.googlesource.com/garnet/+/master/bin/media/media_player/ffmpeg/README.md) for details.  This is
 ignored when building media_player in variant builds (e.g. sanitizers);
 in that case, ffmpeg is always built from source so as to be built with
 the selected variant's config.  When this is false (either explicitly
 or because media_player is a variant build) then //third_party/ffmpeg
 must be in the source tree, which requires:
 `jiri import -name garnet manifest/ffmpeg https://fuchsia.googlesource.com/garnet`

**Current value (from the default):** `true`

### zedboot_cmdline_file
 File containing kernel command line arguments to roll into the
 bootdata image used for zedboot.

**Current value (from the default):** `""`

### crashpad_dependencies
 Determines various flavors of build configuration, and which concrete
 targets to use for dependencies. Valid values are "standalone",
 "chromium", and "fuchsia". Defaulted to "fuchsia" because
 "is_fuchsia_tree" is set.

**Current value (from the default):** `"fuchsia"`

### scudo_default_options
 Default [Scudo](https://llvm.org/docs/ScudoHardenedAllocator.html)
 options (before the `SCUDO_OPTIONS` environment variable is read at
 runtime).  *NOTE:* This affects only components using the `scudo`
 variant (see GN build argument `select_variant`), and does not affect
 anything when the `use_scudo` build flag is set instead.

**Current value (from the default):** `["abort_on_error=1", "QuarantineSizeKb=0", "ThreadLocalQuarantineSizeKb=0"]`

### target_cpu

**Current value for `target_cpu = "arm64"`:** `"arm64"`
	From //root_build_dir/args.gn:1

**Overridden from the default:** `""`

**Current value for `target_cpu = "x64"`:** `"x64"`
	From //root_build_dir/args.gn:1

**Overridden from the default:** `""`

### prebuilt_libvulkan_arm_path

**Current value (from the default):** `""`

### use_ccache
 Set to true to enable compiling with ccache

**Current value (from the default):** `false`

### data_image_size
 The size of the minfs data partition image to create. Normally this image
 is added to FVM, and can therefore expand as needed. It must be at least
 10mb (the default) in order to be succesfully initialized.

**Current value (from the default):** `"10m"`

### is_debug
 Debug build.

**Current value (from the default):** `true`

### magma_enable_tracing
 Enable this to include fuchsia tracing capability

**Current value (from the default):** `true`

## `target_cpu = "arm64"`

### msd_arm_enable_all_cores
 Enable all 8 cores, which is faster but emits more heat.

**Current value (from the default):** `true`

### msd_arm_enable_cache_coherency
 With this flag set the system tries to use cache coherent memory if the
 GPU supports it.

**Current value (from the default):** `true`

## `target_cpu = "x64"`

### mesa_build_root

**Current value (from the default):** `"//third_party/mesa"`

### msd_intel_enable_mapping_cache

**Current value (from the default):** `false`

### use_mock_magma

**Current value (from the default):** `false`

