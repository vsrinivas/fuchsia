<%include file="header.mako" />

load("@fuchsia_sdk//build_defs:fidl.bzl", "fidl_library")

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
