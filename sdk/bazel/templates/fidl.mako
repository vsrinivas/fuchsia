<%include file="header.mako" />

load("//build_defs:cc_fidl_library.bzl", "cc_fidl_library")
load("//build_defs:fidl_library.bzl", "fidl_library")

package(default_visibility = ["//visibility:public"])

fidl_library(
    name = "${data.name}",
    library = "${data.library}",
    srcs = [
        % for source in sorted(data.srcs):
        "${source}",
        % endfor
    ],
    deps = [
        % for dep in sorted(data.deps):
        "${dep}",
        % endfor
    ],
)

cc_fidl_library(
    name = "${data.name}_cc",
    library = ":${data.name}",
)
