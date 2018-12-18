<%include file="header_no_license.mako" />

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

local_repository(
    name = "fuchsia_sdk",
    path = "${data.sdk_path}",
)

load("@fuchsia_sdk//build_defs:fuchsia_setup.bzl", "fuchsia_setup")
fuchsia_setup(
    with_toolchain = ${data.with_cc},
)

% if data.with_dart:
http_archive(
    name = "io_bazel_rules_dart",
    url = "https://github.com/dart-lang/rules_dart/archive/master.zip",
    strip_prefix = "rules_dart-master",
)

load("@io_bazel_rules_dart//dart/build_rules:repositories.bzl", "dart_repositories")
dart_repositories()

load("@fuchsia_sdk//build_defs:setup_dart.bzl", "setup_dart")
setup_dart()

load("@fuchsia_sdk//build_defs:setup_flutter.bzl", "setup_flutter")
setup_flutter()
% endif
