<%include file="header.mako" />

package(default_visibility = ["//visibility:public"])

load("//build_defs:package_files.bzl", "package_files")
load("//build_defs:fuchsia_select.bzl", "fuchsia_select")

# Note: the cc_library / cc_import combo serves two purposes:
#  - it allows the use of a select clause to target the proper architecture;
#  - it works around an issue with cc_import which does not have an "includes"
#    nor a "deps" attribute.
cc_library(
    name = "${data.name}",
    hdrs = [
        % for header in sorted(data.hdrs):
        "${header}",
        % endfor
    ],
    deps = fuchsia_select({
        % for arch in sorted(data.prebuilts.keys()):
        "//build_defs/target_cpu:${arch}": [":${arch}_prebuilts"],
        % endfor
    }) + [
        % for dep in sorted(data.deps):
        "${dep}",
        % endfor
    ],
    strip_include_prefix = "${data.includes}",
    data = fuchsia_select({
        % for arch in sorted(data.prebuilts.keys()):
        "//build_defs/target_cpu:${arch}": [":${arch}_dist"],
        % endfor
    }),
)

# Architecture-specific targets

% for arch, contents in sorted(data.prebuilts.items()):
cc_import(
    name = "${arch}_prebuilts",
    % if data.is_static:
    static_library = "${contents.link_lib}",
    % else:
    shared_library = "${contents.link_lib}",
    % endif
)

package_files(
    name = "${arch}_dist",
    contents = {
        % if contents.dist_lib:
        "${contents.dist_lib}": "${contents.dist_path}",
        % endif
    },
)
% endfor
