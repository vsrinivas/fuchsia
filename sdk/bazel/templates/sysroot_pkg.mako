<%include file="header.mako" />

load("//build_defs:fuchsia_select.bzl", "fuchsia_select")

# This target exists solely for packaging purposes.
alias(
    name = "sysroot",
    actual = fuchsia_select({
        % for arch in sorted(data):
        "//build_defs/target_cpu:${arch}": "//arch/${arch}/sysroot:dist",
        % endfor
    }),
    visibility = [
        "//visibility:public",
    ],
)
