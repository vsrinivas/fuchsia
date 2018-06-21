<%include file="header.mako" />

exports_files(
    glob(["**"]),
)

# For packaging purposes.
cc_library(
    name = "dist",
    srcs = [
        "dist/libc.so",
    ],
    visibility = [
        "//visibility:public",
    ],
)
