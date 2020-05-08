<%include file="header_no_license.mako" />

cc_binary(
    name = "headers",
    srcs = [
        "headers.cc",
    ],
    deps = [
        "@fuchsia_sdk${data['dep']}",
    ],
    copts = [
        "-Wsign-conversion"
    ],
)
