<%include file="header.mako" />

load("@io_bazel_rules_dart//dart/build_rules:core.bzl", "dart_library")

package(default_visibility = ["//visibility:public"])

dart_library(
    name = "${data.name}",
    pub_pkg_name = "${data.package_name}",
    srcs = glob(["lib/**"]),
    deps = [
        % for dep in sorted(data.deps):
        "${dep}",
        % endfor
    ],
)
