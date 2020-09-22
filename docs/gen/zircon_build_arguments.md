# GN Build Arguments

## All builds

### asan_default_options
Default [AddressSanitizer](https://clang.llvm.org/docs/AddressSanitizer.html)
options (before the `ASAN_OPTIONS` environment variable is read at
runtime).  This can be set as a build argument to affect most "asan"
variants in $variants (which see), or overridden in $toolchain_args in
one of those variants.  This can be a list of strings or a single string.

Note that even if this is empty, programs in this build **cannot** define
their own `__asan_default_options` C function.  Instead, they can use a
sanitizer_extra_options() target in their `deps` and then any options
injected that way can override that option's setting in this list.

**Current value for `target_cpu = `:** `[]`

From /b/s/w/ir/k/root_build_dir.zircon/args.gn:4

**Overridden from the default:** `[]`

From //public/gn/config/instrumentation/sanitizer_default_options.gni:16

### assert_level
Controls which asserts are enabled.

`ZX_ASSERT` is always enabled.

* 0 disables standard C `assert()` and `ZX_DEBUG_ASSERT`.
* 1 disables `ZX_DEBUG_ASSERT`. Standard C `assert()` remains enabled.
* 2 enables all asserts.

**Current value (from the default):** `2`

From //public/gn/config/levels.gni:13

### build_id_dir
Directory to populate with `xx/yyy` and `xx/yyy.debug` links to ELF
files.  For every ELF binary built, with build ID `xxyyy` (lowercase
hexadecimal of any length), `xx/yyy` is a hard link to the stripped
file and `xx/yyy.debug` is a hard link to the unstripped file.
Symbolization tools and debuggers find symbolic information this way.

**Current value (from the default):** `"/b/s/w/ir/k/root_build_dir.zircon/.build-id"`

From //public/gn/toolchain/c_toolchain.gni:20

### build_id_format
Build ID algorithm to use for Fuchsia-target code.  This does not apply
to host or guest code.  The value is the argument to the linker's
`--build-id=...` switch.  If left empty (the default), the linker's
default format is used.

**Current value (from the default):** `""`

From //public/gn/config/BUILD.zircon.gn:26

### clang_tool_dir
Directory where the Clang toolchain binaries ("clang", "llvm-nm", etc.) are
found.  If this is "", then the behavior depends on $use_prebuilt_clang.
This toolchain is expected to support both Fuchsia targets and the host.

**Current value (from the default):** `""`

From //public/gn/toolchain/clang.gni:17

### crash_diagnostics_dir
Clang crash reports directory path. Use empty path to disable altogether.

**Current value (from the default):** `"/b/s/w/ir/k/root_build_dir.zircon/clang-crashreports"`

From //public/gn/config/BUILD.zircon.gn:14

### current_cpu

**Current value (from the default):** `""`

### current_os

**Current value (from the default):** `""`

### debuginfo
* `none` means no debugging information
* `backtrace` means sufficient debugging information to symbolize backtraces
* `debug` means debugging information suited for debugging

**Current value (from the default):** `"debug"`

From //public/gn/config/levels.gni:47

### default_deps
Defines the `//:default` target: what `ninja` with no arguments does.
TODO(BLD-353): This must be set by the controlling Fuchsia GN build.

**Current value for `target_cpu = `:** `["//:legacy-x64", "//:legacy_host_targets-linux-x64", "//:legacy_unification-x64", "//tools:all-hosts"]`

From /b/s/w/ir/k/root_build_dir.zircon/args.gn:12

**Overridden from the default:** `false`

From //BUILD.zircon.gn:17

### disable_kernel_pci
Disable kernel PCI driver support. A counterpart of the the build
flag platform_enable_user_pci in //src/devices/bus/drivers/pci/pci.gni.

**Current value for `target_cpu = `:** `false`

From /b/s/w/ir/k/root_build_dir.zircon/args.gn:18

**Overridden from the default:** `false`

From //kernel/params.gni:41

### enable_acpi_debug
Enable debug output in the ACPI library (used by the ACPI bus driver).

**Current value (from the default):** `false`

From //third_party/lib/acpica/BUILD.zircon.gn:11

### enable_lock_dep
Enable kernel lock dependency tracking.

**Current value (from the default):** `false`

From //kernel/params.gni:28

### enable_lock_dep_tests
Enable kernel lock dependency tracking tests.  By default this is
enabled when tracking is enabled, but can also be eanbled independently
to assess whether the tests build and *fail correctly* when lockdep is
disabled.

**Current value (from the default):** `false`

From //kernel/params.gni:68

### environment_args
List of clauses to apply other GN build arguments to specific compilation
environments.  Each clause specifies matching criteria and arguments to
set in such environments.  Each matching clause is applied in order; each
argument it sets overrides any setting of that same argument in an earlier
matching clause or in the environment() declaration.  Note that if the
variant selected for a target via [`variants`](#variants) (which see) has
a `toolchain_args` setting, each argument therein will override the
settings here in `environment_args` clauses (within that variant
toolchain).

Each clause is a scope.  The several parameters listed below are the
matching criteria.  All other parameters in a clause are the build
arguments set when that clause matches.  Note that these form a subset of
the matching criteria supported by [`variants`](#variants) selectors,
except for `tags` and `exclude_tags`.  The semantics of each criterion are
exactly the same here and there.

For example:
```
  environment_args = [ { kernel = true assert_level = 0 } ]
```
sets `assert_level = 0` everywhere where `is_kernel == true`, while:
```
  environment_args = [
    {
      kernel = false
      assert_level = 0
    },
    {
      kernel = true
      assert_level = 1
    },
    {
      environment = [ "efi" ]
      assert_level = 2
      optimize = "none"
    },
  ]
```
sets `assert_level = 0` everywhere where `is_kernel == false`,
sets `assert_level = 1` most places where `is_kernel == true`,
but sets `assert_level = 2` and `optimize = "none"` in the "efi"
environment (where `is_kernel == true` also holds, but the later
clause overrides the preceding `assert_level = 1`).

Clause scope parameters

  * cpu
    - Optional: If nonempty, match only when $current_cpu is one in the
    - list.
    - Type: list(string)

  * os
    - Optional: If nonempty, match only when $current_os is one in the
    - list.
    - Type: list(string)

  * host
    - Optional: If present, match only in host environments if true or
    non-host environments if false.  This means a context in which
    $is_host is true, not specifically the build host.  For example, it
    would be true when cross-compiling host tools for an SDK build but
    would be false when compiling code for a hypervisor guest system
    that happens to be the same CPU and OS as the build host.
    - Type: bool

  * kernel
    - Optional: If present, match only in kernel environments if true or
    non-kernel environments if false.  This means a context in which
    $is_kernel is true, not just the "kernel" environment itself.
    For different machine architectures there may be multiple different
    specialized environments that set $is_kernel, e.g. for boot loaders
    and for special circumstances used within the kernel.  See also the
    $tags field in $variant, described below.
    - Type: bool

  * environment
    - Optional: If nonempty, a list of environment names that match.  This
    looks at ${toolchain.environment}, which is the simple name (no
    directories) in an environment label defined by environment().  Each
    element can match either the whole environment name, or just the
    "base" environment, which is the part of the name before a `.` if it
    has one.  For example, "host" would match both "host" and "host.fuzz".
    - Type: list(string)

  * tags
    - Optional: If nonempty, a list of tags which must be present in the
    `tags` parameter to environment() for that environment to match.
    - Type: list(string)
    - Default: []

  * exclude_tags
    - Optional: If nonempty, a list of tags which must *not* be present in
    the `tags` parameter to environment() for that environment to match.
    - Type: list(string)
    - Default: []


**Current value (from the default):** `[]`

From //public/gn/toolchain/environment.gni:108

### exclude_testonly_syscalls
If true, excludes syscalls with the [testonly] attribute.

**Current value (from the default):** `false`

From //vdso/vdso.gni:7

### fidl_write_v1_wireformat

**Current value (from the default):** `false`

From //system/ulib/fidl/BUILD.zircon.gn:14

### gcc_tool_dir
Directory where the GCC toolchain binaries ("gcc", "nm", etc.) are
found.  If this is "", then the behavior depends on $use_prebuilt_gcc.
This directory is expected to contain `aarch64-elf-*` and `x86_64-elf-*`
tools used to build for the Fuchsia targets.  This directory will not
be used for host tools; if GCC is selected for host builds, only the
system-installed tools found by the shell via `PATH` will be used.

**Current value (from the default):** `""`

From //public/gn/toolchain/gcc.gni:19

### goma_dir
Directory containing the Goma source code.  This can be a GN
source-absolute path ("//...") or a system absolute path.

**Current value for `target_cpu = `:** `"/b/s/w/ir/k/prebuilt/third_party/goma/linux-x64"`

From /b/s/w/ir/k/root_build_dir.zircon/args.gn:19

**Overridden from the default:** `"//prebuilt/third_party/goma/linux-x64"`

From //public/gn/toolchain/goma.gni:17

### host_cpu

**Current value (from the default):** `"x64"`

### host_os

**Current value (from the default):** `"linux"`

### kernel_base

**Current value (from the default):** `"0xffffffff00000000"`

From //kernel/params.gni:22

### kernel_debug_level
Enables various kernel debugging and diagnostic features.  Valid
values are between 0-3.  The higher the value, the more that are
enabled.  A value of 0 disables all of them.

TODO(41790): This value is derived from assert_level.  Decouple
the two and set kernel_debug_level independently.

**Current value (from the default):** `2`

From //kernel/params.gni:53

### kernel_debug_print_level
Controls the verbosity of kernel dprintf messages. The higher the value,
the more dprintf messages emitted. Valid values are 0-2 (inclusive):
  0 - CRITCAL / ALWAYS
  1 - INFO
  2 - SPEW

**Current value (from the default):** `2`

From //kernel/params.gni:60

### kernel_extra_defines
Extra macro definitions for kernel code, e.g. "DISABLE_KASLR",
"ENABLE_KERNEL_LL_DEBUG".

**Current value (from the default):** `[]`

From //kernel/params.gni:45

### kernel_version_string
Version string embedded in the kernel for `zx_system_get_version_string`.
If set to the default "", a string is generated based on the fuchsia.git
revision of the checkout.

**Current value (from the default):** `""`

From //kernel/lib/version/BUILD.zircon.gn:11

### lsan_default_options
Default [LeakSanitizer](https://clang.llvm.org/docs/LeakSanitizer.html)
options (before the `LSAN_OPTIONS` environment variable is read at
runtime).  This can be set as a build argument to affect most "lsan"
variants in $variants (which see), or overridden in $toolchain_args in
one of those variants.  This can be a list of strings or a single string.

Note that even if this is empty, programs in this build **cannot** define
their own `__lsan_default_options` C function.  Instead, they can use a
sanitizer_extra_options() target in their `deps` and then any options
injected that way can override that option's setting in this list.

**Current value for `target_cpu = `:** `[]`

From /b/s/w/ir/k/root_build_dir.zircon/args.gn:5

**Overridden from the default:** `[]`

From //public/gn/config/instrumentation/sanitizer_default_options.gni:28

### mac_sdk_path
Path to Mac SDK.

**Current value (from the default):** `""`

From //public/gn/config/standard.gni:42

### malloc

**Current value (from the default):** `"scudo"`

From //third_party/ulib/musl/BUILD.zircon.gn:6

### optimize
* `none`: really unoptimized, usually only build-tested and not run
* `debug`: "optimized for debugging", light enough to avoid confusion
* `default`: default optimization level
* `size`:  optimized for space rather than purely for speed
* `speed`: optimized purely for speed
* `sanitizer`: optimized for sanitizers (ASan, etc.)
* `profile`: optimized for coverage/profile data collection

**Current value (from the default):** `"default"`

From //public/gn/config/levels.gni:26

### output_breakpad_syms
If true, produce a Breakpad symbol file for each binary.

**Current value (from the default):** `false`

From //public/gn/toolchain/breakpad.gni:9

### output_gsym
Controls whether we should output GSYM files for Fuchsia binaries.

**Current value for `target_cpu = `:** `false`

From /b/s/w/ir/k/root_build_dir.zircon/args.gn:20

**Overridden from the default:** `false`

From //public/gn/toolchain/gsym.gni:10

### rustc_tool_dir
Directory where the Rust toolchain binary ("rustc") is found.  If this is
"", then the prebuilt rustc is used.  Using a system compiler is not
supported.  This toolchain is expected to support both Fuchsia targets and
the host.

**Current value (from the default):** `""`

From //public/gn/toolchain/rustc.gni:13

### rustc_version_string
This is a string identifying the particular toolchain version in use.  Its
only purpose is to be unique enough that it changes when switching to a new
toolchain, so that recompilations with the new compiler can be triggered.

When using the prebuilt, this defaults to the CIPD instance ID of the
prebuilt.

**Current value for `target_cpu = `:** `"0v1jaeyeb9K3EGyl_O56bQ02Nt1CCd3_JeANRsXANBUC"`

From /b/s/w/ir/k/root_build_dir.zircon/args.gn:21

**Overridden from the default:** `""`

From //public/gn/toolchain/rustc.gni:21

### scheduler_tracing_level
The level of detail for scheduler traces when enabled. Values greater than
zero add increasing details at the cost of increased trace buffer use.

0 = Default kernel:sched tracing.
1 = Adds duration traces for key scheduler operations.
2 = Adds flow events from wakeup to running state.
3 = Adds detailed internal durations and probes.

**Current value (from the default):** `0`

From //kernel/params.gni:37

### smp_max_cpus

**Current value (from the default):** `16`

From //kernel/params.gni:14

### sysroot
The `--sysroot` directory for host compilations.
This can be a string, which only applies to $host_os-$host_cpu.
Or it can be a list of scopes containing `cpu`, `os`, and `sysroot`.
The empty list (or empty string) means don't use `--sysroot` at all.

**Current value (from the default):**
```
[{
  cpu = "arm64"
  os = "linux"
  sysroot = "//../prebuilt/third_party/sysroot/linux"
}, {
  cpu = "x64"
  os = "linux"
  sysroot = "//../prebuilt/third_party/sysroot/linux"
}]
```

From //public/gn/config/BUILD.zircon.gn:20

### target_cpu

**Current value (from the default):** `""`

### target_os

**Current value (from the default):** `""`

### thinlto_cache_dir
ThinLTO cache directory path.

**Current value (from the default):** `"host-arm64-linux-lto/thinlto-cache"`

From //public/gn/config/lto/BUILD.zircon.gn:22

### thinlto_jobs
Number of parallel ThinLTO jobs.

**Current value (from the default):** `8`

From //public/gn/config/lto/BUILD.zircon.gn:19

### toolchain
*This must never be set as a build argument.*
It exists only to be set via c_toolchain().
See environment() for more information.

**Current value (from the default):**
```
{
  configs = []
  environment = "stub"
  globals = { }
  label = "//public/gn/toolchain:stub"
}
```

From //public/gn/BUILDCONFIG.gn:30

### ubsan_default_options
Default [UndefinedBehaviorSanitizer](https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html)
options (before the `UBSAN_OPTIONS` environment variable is read at
runtime).  This can be set as a build argument to affect most "ubsan"
variants in $variants (which see), or overridden in $toolchain_args in
one of those variants.  This can be a list of strings or a single string.

Note that even if this is empty, programs in this build **cannot** define
their own `__ubsan_default_options` C function.  Instead, they can use a
sanitizer_extra_options() target in their `deps` and then any options
injected that way can override that option's setting in this list.

**Current value for `target_cpu = `:** `["print_stacktrace=1", "halt_on_error=1"]`

From /b/s/w/ir/k/root_build_dir.zircon/args.gn:6

**Overridden from the default:** `["print_stacktrace=1", "halt_on_error=1"]`

From //public/gn/config/instrumentation/sanitizer_default_options.gni:40

### use_ccache
Set to true to enable compiling with ccache.

**Current value for `target_cpu = `:** `false`

From /b/s/w/ir/k/root_build_dir.zircon/args.gn:22

**Overridden from the default:** `false`

From //public/gn/toolchain/ccache.gni:9

### use_goma
Set to true to enable distributed compilation using Goma.

**Current value for `target_cpu = `:** `false`

From /b/s/w/ir/k/root_build_dir.zircon/args.gn:23

**Overridden from the default:** `false`

From //public/gn/toolchain/goma.gni:13

### use_prebuilt_clang
If $clang_tool_dir is "", then this controls how the Clang toolchain
binaries are found.  If true, then the standard prebuilt is used.
Otherwise the tools are just expected to be found by the shell via `PATH`.

**Current value (from the default):** `true`

From //public/gn/toolchain/clang.gni:12

### use_prebuilt_gcc
If $gcc_tool_dir is "", then this controls how the GCC toolchain
binaries are found.  If true, the standard prebuilt is used.  If false,
the tools are just expected to be found in PATH.

**Current value (from the default):** `true`

From //public/gn/toolchain/gcc.gni:11

### variants
List of "selectors" to request variant builds of certain targets.  Each
selector specifies matching criteria and a chosen variant.  The first
selector in the list to match a given target determines which variant is
used for that target.

The $default_variants list is appended to the list set here.  So if no
selector set in $variants matches (e.g. if the list is empty, as is the
default), then the first match in $default_variants chooses the variant.

Each selector is either a string or a scope.  A selector that's a string
is a shorthand that gets expanded to a full selector (a scope); the full
selector form is described below.

If a string selector contains a slash, then it's "shorthand/filename".
This is like the plain "shorthand" selector, but further constrained to
apply only to a binary whose `output_name` exactly matches "filename".

The "shorthand" string (a whole string selector or the part before slash)
is first looked up in $variant_shorthands, which see.  If it doesn't match
a name defined there, then it must be the name of a variant.  In that case,
it's equivalent to `{ variant = "..." host = false }`, meaning it applies
to every binary not built to be a host tool.

A full selector is a scope with the following fields.  All the fields
other than `.variant` are matching criteria.  A selector matches if all of
its matching criteria match.  Hence, a selector with no criteria defined
always matches and is referred to as a "catch-all".  The $default_variants
list ends with a catch-all, so each target always chooses some variant.

Selector scope parameters

  * variant
    - Required: The variant to use when this selector matches.  If this
    is a string then it must match a fully-defined variant elsewhere in
    the list (or in $default_variants + $standard_variants, which is
    appended implicitly to the $variants list).  If it's a scope then
    it defines a new variant (see details below).
    - Type: string or scope, described below

  * cpu
    - Optional: If nonempty, match only when $current_cpu is one in the
    - list.
    - Type: list(string)

  * os
    - Optional: If nonempty, match only when $current_os is one in the
    - list.
    - Type: list(string)

  * host
    - Optional: If present, match only in host environments if true or
    non-host environments if false.  This means a context in which
    $is_host is true, not specifically the build host.  For example, it
    would be true when cross-compiling host tools for an SDK build but
    would be false when compiling code for a hypervisor guest system
    that happens to be the same CPU and OS as the build host.
    - Type: bool

  * kernel
    - Optional: If present, match only in kernel environments if true or
    non-kernel environments if false.  This means a context in which
    $is_kernel is true, not just the "kernel" environment itself.
    For different machine architectures there may be multiple different
    specialized environments that set $is_kernel, e.g. for boot loaders
    and for special circumstances used within the kernel.  See also the
    $tags field in $variant, described below.
    - Type: bool

  * environment
    - Optional: If nonempty, a list of environment names that match.  This
    looks at ${toolchain.environment}, which is the simple name (no
    directories) in an environment label defined by environment().  Each
    element can match either the whole environment name, or just the
    "base" environment, which is the part of the name before a `.` if it
    has one.  For example, "host" would match both "host" and "host.fuzz".
    - Type: list(string)

  * target_type
    - Optional: If nonempty, a list of target types to match.  This is
    one of "executable", "host_tool", "loadable_module", "driver", or
    "test".
    Note, test_driver() matches as "driver".
    - Type: list(string)

  * label
    - Optional: If nonempty, match only when the canonicalized target label
    (as returned by `get_label_info(..., "label_no_toolchain")`) is one in
    the list.
    - Type: list(label_no_toolchain)

  * dir
    - Optional: If nonempty, match only when the directory part of the
    target label (as returned by `get_label_info(..., "dir")`) is one in
    the list.
    - Type: list(label_no_toolchain)

  * name
    - Optional: If nonempty, match only when the name part of the target
    label (as returned by `get_label_info(..., "name")`) is one in the
    list.
    - Type: list(label_no_toolchain)

  * output_name
    - Optional: If nonempty, match only when the `output_name` of the
    target is one in the list.  Note `output_name` defaults to
    `target_name`, and does not include prefixes or suffixes like ".so"
    or ".exe".
    - Type: list(string)

An element with a scope for `.variant` defines a new variant.  Each
variant name used in a selector must be defined exactly once.  Other
selectors can refer to the same variant by using the name string in the
`.variant` field.  Definitions in $variants take precedence over the same
name defined in $standard_variants, but it would probably cause confusion
to use the name of a standard variant with a non-standard definition.

Variant scope parameters

  * name
    - Required: Name for the variant.  This must be unique among all
    variants used with the same environment.  It becomes part of the GN
    toolchain names defined for the environment, which in turn forms part
    of directory names used in $root_build_dir; so it must meet Ninja's
    constraints on file names (sticking to `[a-z0-9_-]` is a good idea).

  * globals
    - Optional: Variables in this scope are introduced as globals visible
    to all GN code in the toolchain.  For example, the standard "gcc"
    variant sets `is_gcc = true` in $globals.  This should be used
    sparingly and is safest when restricted to variables that
    $zx/public/gn/BUILDCONFIG.gn sets defaults for.
    - Type: scope

  * toolchain_args
    - Optional: See toolchain().  Variables in this scope must match GN
    build arguments defined somewhere in the build with declare_args().
    Use this when the variant should change something that otherwise is a
    manual tuning variable to set via `gn args`.  *Do not* define
    variables in declare_args() just for the purpose of setting them here,
    i.e. if they should not *also* be available to set via `gn args` to
    affect other variants that don't override them here.  Instead, use
    either $globals (above) or $toolchain_vars (below).
    - Type: scope

  * toolchain_vars
    - Optional: Variables in this scope are visible in the scope-typed
    $toolchain global variable seen in toolchains for this variant.
    Use this to pass along interesting information without cluttering
    the global scope via $globals.
    - Type: scope

  * configs
    - Optional: List of changes to the pre-set $configs variable in targets
    being defined in toolchains for this variant.  This is the same as in
    the $configs parameter to environment().  Each element is either a
    string or a scope.  A string element is simply appended to the default
    $configs list: it's equivalent to a scope element of `{add=["..."]}`.
    The string is the GN label (without toolchain) for a config() target.
    A scope element can be more selective, as described below.
    - Type: list(label_no_toolchain or scope)
      * shlib
        - Optional: If present, this element applies only when
        `current_toolchain == toolchain.shlib` (if true) or
        `current_toolchain != toolchain.shlib` (if false).  That is, it
        applies only in (not ni) the companion toolchain used to compile
        shared_library() and loadable_module() (including driver()) code.
        - Type: bool

      * types
        - Optional: If present, this element applies only to a target whose
        type is one in this list (same as `target_type` in a selector,
        described above).
        - Type: list(string)

      * add
        - Optional: List of labels to append to $configs.
        - Type: list(label_no_toolchain)

      * remove
        - Optional: List of labels to remove from $configs.  This does
        exactly `configs -= remove` so it has the normal GN semantics that
        it's an error if any element in the $remove list is not present in
        the $configs list beforehand.
        - Type: list(label_no_toolchain)

  * implicit_deps
    - Optional: List of changes to the list added to $deps of all linking
    targets in toolchains for this variant.  This is the same as in the
    $implicit_deps parameter to environment().
    - Type: See $configs

  * tags
    - Optional: List of tags that describe this variant.  This list will be
    visible within the variant's toolchains as ${toolchain.tags}.  Its main
    purpose is to match the $exclude_variant_tags list in an environment()
    definition.  For example, several of the standard variants listed in
    $standard_variants use the "useronly" tag.  The environment() defining
    the kernel toolchains uses `exclude_variant_tags = [ "useronly" ]`.
    Then $variants selectors that choose variants that are incompatible
    with the kernel are automatically ignored in the kernel toolchains,
    so there's no need to add `kernel = false` to every such selector.
    - Type: list(string)

  * bases
    - Optional: A list of other variant names that this one inherits from.
    This is a very primitive mechanism for deriving a new variant from an
    existing variant.  All of fields from all the bases except for `name`
    and `bases` are combined with the fields defined explicitly for the
    new variant.  The fields of list type are just concatenated in order
    (each $bases variant in the order listed, then this variant).  The
    fields of scope type are merged in the same order, with a variant
    later in the list overriding values set earlier (so this variant's
    values override all the bases).  There is *only one* level of
    inheritance: a base variant listed in $bases cannot have $bases itself.
    - Type: list(string)


**Current value for `target_cpu = `:** `[]`

From /b/s/w/ir/k/root_build_dir.zircon/args.gn:24

**Overridden from the default:** `[]`

From //public/gn/toolchain/variants.gni:222

### zbi_compression
This can be "zstd", optionally followed by ".LEVEL" where `LEVEL` can be an
integer or "max".  It can also be just "LEVEL" to to use the default
algorithm with a non-default setting.

The default level for each algorithm is tuned to balance compression
speed with compression ratio.  Higher levels make image builds slower.
So using the default during rapid development (quick builds, pretty
good compression) and "max' for production builds (slow builds, best
compression available) probably makes sense.

**Current value for `target_cpu = `:** `"zstd"`

From /b/s/w/ir/k/root_build_dir.zircon/args.gn:25

**Overridden from the default:** `"zstd"`

From //public/gn/zbi.gni:19

### zx
"$zx/" is the prefix for GN "source-absolute" paths in the Zircon
build.  When Zircon is built standalone, the Zircon repository is the
root of the build (where `.gn` is found) so "$zx/" becomes "//".  When
Zircon is part of a larger unified build, there is a higher-level `.gn`
file that uses `default_args` to set "$zx/" to "//zircon/".

**Current value (from the default):** `"/"`

From //public/gn/BUILDCONFIG.gn:13

### zx_build
"$zx_build/" is the prefix for GN "source-absolute" paths in the Zircon
build for build infrastructure.

**Current value (from the default):** `"/"`

From //public/gn/BUILDCONFIG.gn:17

### zx_build_config
"$zx_build_config" is the directory containing GN configs used by
the Zircon build infrastructure. This allows referring to them
with "$zx_build_config:<name>" in BUILD.zircon.gn and BUILD.gn
files.

**Current value (from the default):** `"//public/gn/config"`

From //public/gn/BUILDCONFIG.gn:23

### zx_fidl_trace_level
This mirrors the fidl_trace_level GN variable in //build/fidl/args.gni to
the ZN build. See that file for more information about what this variable
does.

**Current value for `target_cpu = `:** `0`

From /b/s/w/ir/k/root_build_dir.zircon/args.gn:26

**Overridden from the default:** `0`

From //public/gn/fidl/params.gni:9

