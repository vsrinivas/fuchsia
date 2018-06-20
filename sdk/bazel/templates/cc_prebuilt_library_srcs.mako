filegroup(
    name = "${data.target_arch}_prebuilts",
    srcs = [
        % for source in sorted(data.srcs):
        "${source}",
        % endfor
    ],
)
