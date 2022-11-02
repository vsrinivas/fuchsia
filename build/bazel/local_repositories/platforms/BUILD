package(default_visibility = ["//visibility:public"])

licenses(["notice"])

exports_files(["LICENSE"])

filegroup(
    name = "srcs",
    srcs = [
        "BUILD",
        "WORKSPACE",
        "//cpu:srcs",
        "//os:srcs",
    ],
)

# For use in Incompatible Target Skipping:
# https://docs.bazel.build/versions/main/platforms.html#skipping-incompatible-targets
#
# Specifically this lets targets declare incompatibility with some set of
# platforms. See
# https://docs.bazel.build/versions/main/platforms.html#more-expressive-constraints
# for some more details.
constraint_setting(name = "incompatible_setting")

constraint_value(
    name = "incompatible",
    constraint_setting = ":incompatible_setting",
)
