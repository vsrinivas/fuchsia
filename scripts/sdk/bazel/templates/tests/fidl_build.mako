<%include file="header_no_license.mako" />

# This tests checks that symbols are not accidentally added to generated headers
# instead of sources.
cc_binary(
    name = "header_test.so",
    srcs = [
        "header_test.cc",
    ],
    deps = [
        "@fuchsia_sdk//fidl/${data['library']}:${data['library']}_cc",
    ],
    linkshared = True,
)
