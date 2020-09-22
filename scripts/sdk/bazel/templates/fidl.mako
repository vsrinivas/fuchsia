<%include file="header.mako" />

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
        "//fidl/${dep}",
        % endfor
    ],
)

% if data.with_cc:
load("//build_defs:cc_fidl_library.bzl", "cc_fidl_library")

cc_fidl_library(
    name = "${data.name}_cc",
    library = ":${data.name}",
    # TODO(fxbug.dev/5205): remove explicit deps once C++ compilation API is available
    #     in Skylark and generated through the cc_fidl_library rule.
    deps = [
        % for dep in sorted(data.deps):
        "//fidl/${dep}:${dep}_cc",
        % endfor
    ],
)
% endif

% if data.with_dart:
load("//build_defs:dart_fidl_library.bzl", "dart_fidl_library")

dart_fidl_library(
    name = "${data.name}_dart",
    deps = [":${data.name}"],
)
% endif
