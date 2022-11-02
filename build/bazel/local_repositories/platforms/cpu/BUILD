# Standard cpu name constraint_setting and constraint_values
licenses(["notice"])

package(
    default_visibility = ["//visibility:public"],
)

filegroup(
    name = "srcs",
    srcs = glob(["**"]),
)

# To add a new constraint_value see https://github.com/bazelbuild/platforms.
constraint_setting(name = "cpu")

# TODO(b/136237408): Remove this generic CPU name and replace with a specific one.
alias(
    name = "aarch64",
    actual = ":arm64",
)

# TODO(b/136237408): Remove this generic CPU name and replace with a specific one.
constraint_value(
    name = "arm",
    constraint_setting = ":cpu",
)

# Cortex-M0, Cortex-M0+, Cortex-M1
constraint_value(
    name = "armv6-m",  # Commonly known as thumbv6
    constraint_setting = ":cpu",
)

# Cortex-M3
constraint_value(
    name = "armv7-m",
    constraint_setting = ":cpu",
)

# Cortex-M4, Cortex-M7
constraint_value(
    name = "armv7e-m",
    constraint_setting = ":cpu",
)

# Cortex-M4, Cortex-M7 with fpu
constraint_value(
    name = "armv7e-mf",  # armv7e-m with fpu
    constraint_setting = ":cpu",
)

# Cortex-M23, Cortex-M33, Cortex-M35P
constraint_value(
    name = "armv8-m",
    constraint_setting = ":cpu",
)

constraint_value(
    name = "arm64_32",
    constraint_setting = ":cpu",
)

constraint_value(
    name = "arm64",
    constraint_setting = ":cpu",
)

constraint_value(
    name = "arm64e",
    constraint_setting = ":cpu",
)

constraint_value(
    name = "armv7",
    constraint_setting = ":cpu",
)

constraint_value(
    name = "armv7k",
    constraint_setting = ":cpu",
)

constraint_value(
    name = "i386",
    constraint_setting = ":cpu",
)

constraint_value(
    name = "ppc",
    constraint_setting = ":cpu",
)

constraint_value(
    name = "s390x",
    constraint_setting = ":cpu",
)

constraint_value(
    name = "x86_32",
    constraint_setting = ":cpu",
)

constraint_value(
    name = "x86_64",
    constraint_setting = ":cpu",
)

constraint_value(
    name = "wasm32",
    constraint_setting = ":cpu",
)

constraint_value(
    name = "wasm64",
    constraint_setting = ":cpu",
)

constraint_value(
    name = "mips64",
    constraint_setting = ":cpu",
)

constraint_value(
    name = "riscv32",
    constraint_setting = ":cpu",
)

constraint_value(
    name = "riscv64",
    constraint_setting = ":cpu",
)
