<%include file="header.mako" />

load(":dist.bzl", "toolchain_dist")

# TODO(pylaligand): make this target configurable by developers. This is blocked
# by current work on Bazel build configuration (design at
# https://docs.google.com/document/d/1vc8v-kXjvgZOdQdnxPTaV0rrLxtP2XwnD2tAZlYJOqw/edit?usp=sharing)
toolchain_dist(
    name = "dist",
    files = select({
        % for arch in data.arches:
        "//build_defs/target_cpu:${arch.short_name}": "@fuchsia_crosstool//:dist-${arch.long_name}",
        % endfor
    }),
    visibility = [
        "//visibility:public",
    ],
)
