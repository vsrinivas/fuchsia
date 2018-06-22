<%include file="header.mako" />

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "${data.name}",
    srcs = select({
        "//build_defs/target_cpu:arm64": [":arm64_prebuilts"],
        "//build_defs/target_cpu:x64": [":x64_prebuilts"],
    }),
    hdrs = [
        % for header in sorted(data.hdrs):
        "${header}",
        % endfor
    ],
    deps = [
        % for dep in sorted(data.deps):
        "${dep}",
        % endfor
    ],
    includes = [
        % for include in sorted(data.includes):
        "${include}",
        % endfor
    ],
)

# Target specific dependencies
