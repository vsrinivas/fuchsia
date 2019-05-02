# GN Build Arguments

## All builds

### current_cpu

**Current value (from the default):** `""`

### enable_user_pci
Enable userspace PCI and disable kernel PCI.

**Current value (from the default):** `false`

From //kernel/params.gni:42

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

From //public/gn/BUILDCONFIG.gn:20

### crash_diagnostics_dir
Clang crash reports directory path. Use empty path to disable altogether.

**Current value (from the default):** `"/b/s/w/ir/k/out/build-zircon/clang-crashreports"`

From //public/gn/config/BUILD.gn:10

### default_deps
Defines the `//:default` target: what `ninja` with no arguments does.

**Current value (from the default):** `[":ids", ":images", ":tools"]`

From //BUILD.gn:19

### enable_lock_dep
Enable kernel lock dependency tracking.

**Current value (from the default):** `false`

From //kernel/params.gni:32

### goma_dir
Absolute directory containing the Goma source code.

**Current value (from the default):** `"/home/swarming/goma"`

From //public/gn/toolchain/goma.gni:12

### clang_tool_dir
Directory where the Clang toolchain binaries ("clang", "llvm-nm", etc.) are
found.  If this is "", then the behavior depends on $use_prebuilt_clang.
This toolchain is expected to support both Fuchsia targets and the host.

**Current value (from the default):** `""`

From //public/gn/toolchain/clang.gni:14

### detailed_scheduler_tracing
Enable detailed scheduler traces.

**Current value (from the default):** `false`

From //kernel/params.gni:39

### enable_kernel_debugging_features
Whether to include various features (non-shipping, insecure, etc.) in the
kernel build.

**Current value (from the default):** `false`

From //BUILD.gn:27

### host_cpu

**Current value (from the default):** `"x64"`

### kernel_extra_defines
Extra macro definitions for kernel code, e.g. "DISABLE_KASLR",
"ENABLE_KERNEL_LL_DEBUG".

**Current value (from the default):** `[]`

From //kernel/params.gni:46

### tests_in_image
Whether to include all the Zircon tests in the main standalone ZBI.
TODO(mcgrathr): This will be replaced by a more sophisticated plan for
what images to build rather than a single "everything" image that needs
to be pared down.

**Current value (from the default):** `true`

From //BUILD.gn:16

### assert_level
* 0 means no assertions, not even standard C `assert()`.
* 1 means `ZX_ASSERT` but not `ZX_DEBUG_ASSERT`.
* 2 means both `ZX_ASSERT` and  `ZX_DEBUG_ASSERT`.

**Current value (from the default):** `2`

From //public/gn/config/levels.gni:9

### kernel_base

**Current value (from the default):** `"0xffffffff80100000"`

From //kernel/params.gni:20

### kernel_version_string
Version string embedded in the kernel for `zx_system_get_version`.
If set to the default "", a string is generated based on the
Zircon git revision of the checkout.

**Current value (from the default):** `""`

From //kernel/lib/version/BUILD.gn:9

### opt_level
* -1 means really unoptimized (-O0), usually only build-tested and not run.
* 0 means "optimized for debugging" (-Og), light enough to avoid confusion.
  1, 2, and 3 are increasing levels of optimization.
* 4 is optimized for space rather than purely for speed.

**Current value (from the default):** `2`

From //public/gn/config/levels.gni:15

### symbol_level
* 0 means no debugging information.
* 1 means minimal debugging information sufficient to symbolize backtraces.
* 2 means full debugging information for use with a symbolic debugger.

**Current value (from the default):** `2`

From //public/gn/config/levels.gni:20

### target_cpu

**Current value (from the default):** `""`

### target_os

**Current value (from the default):** `""`

### use_goma
Set to true to enable distributed compilation using Goma.

**Current value (from the default):** `false`

From //public/gn/toolchain/goma.gni:9

### use_prebuilt_clang
If $clang_tool_dir is "", then this controls how the Clang toolchain
binaries are found.  If true, then the standard prebuilt is used.
Otherwise the tools are just expected to be found by the shell via `PATH`.

**Current value (from the default):** `true`

From //public/gn/toolchain/clang.gni:9

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


**Current value (from the default):** `[]`

From //public/gn/toolchain/environment.gni:225

### host_os

**Current value (from the default):** `"linux"`

### smp_max_cpus
Maximum number of CPUs the kernel will run on (others will be ignored).

**Current value (from the default):** `32`

From //kernel/params.gni:7

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
  sysroot = "//prebuilt/downloads/sysroot/linux-arm64"
}, {
  cpu = "x64"
  os = "linux"
  sysroot = "//prebuilt/downloads/sysroot/linux-x64"
}]
```

From //public/gn/config/BUILD.gn:16

### current_os

**Current value (from the default):** `""`

### enable_acpi_debug
Enable debug output in the ACPI library (used by the ACPI bus driver).

**Current value (from the default):** `false`

From //third_party/lib/acpica/BUILD.gn:9

### enable_fair_scheduler
Disable fair scheduler by default on all architectures.
ZX-3959: Disable by default until E2E tests stabilize.

**Current value (from the default):** `false`

From //kernel/params.gni:36

### gcc_tool_dir
Directory where the GCC toolchain binaries ("gcc", "nm", etc.) are
found.  If this is "", then the behavior depends on $use_prebuilt_gcc.
This directory is expected to contain `aarch64-elf-*` and `x86_64-elf-*`
tools used to build for the Fuchsia targets.  This directory will not
be used for host tools; if GCC is selected for host builds, only the
system-installed tools found by the shell via `PATH` will be used.

**Current value (from the default):** `""`

From //public/gn/toolchain/gcc.gni:17

### malloc

**Current value (from the default):** `"scudo"`

From //third_party/ulib/musl/BUILD.gn:6

### zx
*This must never be set as a build argument*.

"$zx/" is the prefix for GN "source-absolute" paths in the Zircon
build.  When Zircon is built standalone, the Zircon repository is the
root of the build (where `.gn` is found) so "$zx/" becomes "//".  When
Zircon is part of a larger unified build, there is a higher-level `.gn`
file that uses `default_args` to set "$zx/" to "//zircon/".

**Current value (from the default):** `"/"`

From //public/gn/BUILDCONFIG.gn:13

### asan_default_options
Default [AddressSanitizer](https://llvm.org/docs/AddressSanitizer.html)
options (before the `ASAN_OPTIONS` environment variable is read at
runtime).  This can be set as a build argument to affect most "asan"
variants in $variants (which see), or overridden in $toolchain_args in
one of those variants.  Note that setting this nonempty may conflict
with programs that define their own `__asan_default_options` C
function.

**Current value (from the default):** `""`

From //public/gn/config/instrumentation/BUILD.gn:13

### netsvc_debug_commands
Whether to enable debug commands in netsvc.

**Current value (from the default):** `true`

From //system/core/netsvc/BUILD.gn:7

### use_ccache
Set to true to enable compiling with ccache.

**Current value (from the default):** `false`

From //public/gn/toolchain/ccache.gni:9

### build_id_dir
Directory to populate with `xx/yyy` and `xx/yyy.debug` links to ELF
files.  For every ELF binary built, with build ID `xxyyy` (lowercase
hexadecimal of any length), `xx/yyy` is a hard link to the stripped
file and `xx/yyy.debug` is a hard link to the unstripped file.
Symbolization tools and debuggers find symbolic information this way.

**Current value (from the default):** `"/b/s/w/ir/k/out/build-zircon/.build-id"`

From //public/gn/toolchain/c_toolchain.gni:17

### enable_lock_dep_tests
Enable kernel lock dependency tracking tests.  By default this is
enabled when tracking is enabled, but can also be eanbled independently
to assess whether the tests build and *fail correctly* when lockdep is
disabled.

**Current value (from the default):** `false`

From //kernel/params.gni:54

### kernel_aspace_base

**Current value (from the default):** `"0xffffff8000000000UL"`

From //kernel/params.gni:28

### use_prebuilt_gcc
If $gcc_tool_dir is "", then this controls how the GCC toolchain
binaries are found.  If true, the standard prebuilt is used.  If false,
the tools are just expected to be found in PATH.

**Current value (from the default):** `true`

From //public/gn/toolchain/gcc.gni:9

