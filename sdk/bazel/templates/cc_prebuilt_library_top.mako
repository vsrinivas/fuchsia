<%include file="header.mako" />

package(default_visibility = ["//visibility:public"])

load("//build_defs:package_files.bzl", "package_files")

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
    deps = select({
        "//build_defs/target_cpu:arm64": [":arm64_prebuilts"],
        "//build_defs/target_cpu:x64": [":x64_prebuilts"],
    }) + [
        % for dep in sorted(data.deps):
        "${dep}",
        % endfor
    ],
    includes = [
        % for include in sorted(data.includes):
        "${include}",
        % endfor
    ],
    data = select({
        "//build_defs/target_cpu:arm64": [":arm64_dist"],
        "//build_defs/target_cpu:x64": [":x64_dist"],
    }),
)

# Architecture-specific targets
