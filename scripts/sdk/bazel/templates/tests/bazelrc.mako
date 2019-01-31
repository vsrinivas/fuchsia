% for arch in data.arches:
build:fuchsia_${arch.short_name} --crosstool_top=@fuchsia_crosstool//:toolchain
build:fuchsia_${arch.short_name} --cpu=${arch.long_name}
build:fuchsia_${arch.short_name} --host_crosstool_top=@bazel_tools//tools/cpp:toolchain
% endfor
