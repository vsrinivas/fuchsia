<%include file="header_no_license.mako" />

cc_binary(
    name = "headers",
    srcs = [
        "headers.cc",
    ],
    deps = [
        % for dep in sorted(data['deps']):
        "@fuchsia_sdk${dep}",
        % endfor
    ],
)
