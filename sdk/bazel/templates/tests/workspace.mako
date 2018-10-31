<%include file="header_no_license.mako" />

local_repository(
    name = "fuchsia_sdk",
    path = "${data.sdk_path}",
)

% if data.with_cc:
load("@fuchsia_sdk//build_defs:crosstool.bzl", "install_fuchsia_crosstool")
install_fuchsia_crosstool(
name = "fuchsia_crosstool",
)
% endif

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
