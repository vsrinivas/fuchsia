<%include file="header.mako" />

assert(current_os == "fuchsia")

import("build/fuchsia_sdk_pkg.gni")

config("sdk_lib_dirs_config") {
  visibility = [ ":*" ]
  lib_dirs = [ <%text>"sdk/arch/${target_cpu}/lib"</%text> ]
}

# Copy the loader to place it at the expected path in the final package.
copy("sysroot_dist_libs") {
  sources = [
    <%text>"sdk/arch/${target_cpu}/sysroot/dist/lib/ld.so.1",</%text>
  ]

  outputs = [
    <%text>"${root_out_dir}/lib/{{source_file_part}}",</%text>
  ]
}

# This adds the runtime deps for //build/config/compiler:runtime_library
# as that is a config target and thus cannot include data_deps.
group("runtime_library") {
  data_deps = [
    ":sysroot_dist_libs",

    # This is used directly from //build/config/fuchsia:compiler and thus
    # also needs to be included by default.
    "sdk/pkg/fdio",
  ]
}

group("all_fidl_targets") {
  deps = [
    % for dep in sorted(data.fidl_targets):
    "sdk/fidl/${dep}",
    % endfor
  ]
}
