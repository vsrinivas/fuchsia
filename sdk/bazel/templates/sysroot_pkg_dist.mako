
alias(
    name = "${data.target_arch}_dist",
    actual = "//arch/${data.target_arch}/sysroot:dist",
)
