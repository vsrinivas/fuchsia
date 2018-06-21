<%include file="header.mako" />

load("//build_defs:dart_library.bzl", "dart_library")

package(default_visibility = ["//visibility:public"])

dart_library(
    name = "${data.name}",
    package_name = "${data.package_name}",
    source_dir = "lib",
    deps = [
        % for dep in sorted(data.deps):
        "${dep}",
        % endfor
    ],
)
