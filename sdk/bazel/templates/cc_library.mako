<%include file="header.mako" />

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "${data.name}",
    srcs = [
        % for source in sorted(data.srcs):
        "${source}",
        % endfor
    ],
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
