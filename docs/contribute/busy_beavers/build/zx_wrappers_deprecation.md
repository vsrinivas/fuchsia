# `zx_*` wrappers deprecation

<<../_stub_banner.md>>

## Goal & motivation

Historically, Fuchsia's build system contained an inner build system to build a
subset of the code that we loosely referred to as Zircon. This included the
Zircon kernel and associated libraries, as well as code that was at some point
organizationally or technically associated with the kernel or the people who
worked on the kernel.

All the build definitions have since been migrated to a single system, but some
build definitions still carry the legacy of the previous system.
Particularly there still remain two legacy wrappers for build templates:

*   `zx_library()`
*   `zx_host_tool()`

These templates wrap other common build templates with some additional logic
which was meant to enforce a common structure of C++ code and headers in
Zircon. Over time they've evolved to carry other logic, such as for publishing
artifacts to the SDK, that is also achieved by other templates, and hence is
duplicate and may be confusing. Finally, some of the most common use cases can
be achieved with standard GN target types which are more familiar and well
documented.

## Technical background

General experience in working with `BUILD.gn` files is recommended but not
entirely necessary.
Please consult the [GN reference][gn-reference]i{:.external} guide.

## How to help

### Picking a task

Start by finding any instance of `zx_library` or `zx_host_tool` in a build file.

```gn
zx_library("foo") {
  ...
}
```

Alternatively, there exists an allowlist listing all existing directories where
the old templates are still being used, whether directly or indirectly via
another wrapper. You can find the allowlist in
[`//build/BUILD.gn`](/build/BUILD.gn).
under the group `"deprecated_zx_wrapper_allowlist"`.

### Doing a task

Rewrite targets that use the `zx_*` wrappers using other templates.

Replace `zx_library` with one of the following:

*   `source_set`
*   `sdk_source_set`
*   `static_library`
*   `sdk_static_library`
*   `shared_library`
*   `sdk_shared_library`

Replace `zx_host_tool` with the built-in `executable` rule and using the host
toolchain as needed. If the tool is used in the SDK, then you may also need to
define an `sdk_atom` target. There is a convenience wrapper at
[`//build/sdk/sdk_host_tool.gni`](/build/sdk/sdk_host_tool.gni) just for that.

As you run into common failure modes and solutions, please consider documenting
them here for reference.

### Completing a task

When preparing your change, make sure to remove any lines from
[`//build/BUILD.gn`](/build/BUILD.gn)
listing the directories that you cleaned up.

Send the change for review using the regular process.

## Examples

This guide is missing an example. Multiple examples may be beneficial in this
case. If making changes to support this project, please consider adding them
here for reference.

## Sponsors

Reach out for questions or for status updates:

*   <digit@google.com>
*   <pylaligand@google.com>
*   <shayba@google.com>

[gn-reference]: https://gn.googlesource.com/gn/+/master/docs/reference.md
