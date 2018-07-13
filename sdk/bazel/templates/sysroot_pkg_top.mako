<%include file="header.mako" />

# This target exists solely for packaging purposes.
# The double indirection with target-specific aliases is a by-product of Bazel
# requiring that labels in a select clause correspond to existing packages,
# whereas some SDKs won't contain all possible architectures.
# By pointing to a target in the current package, we make Bazel happy - even
# though some of the local targets might not exist at all.
alias(
    name = "sysroot",
    actual = select({
        "//build_defs/target_cpu:arm64": ":arm64_dist",
        "//build_defs/target_cpu:x64": ":x64_dist",
    }),
    visibility = [
        "//visibility:public",
    ],
)

# Architecture-specific targets
