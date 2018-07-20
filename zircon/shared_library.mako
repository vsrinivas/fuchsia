<%include file="header.mako" />

import("//build/sdk/sdk_atom.gni")

config("${data.name}_config") {
  include_dirs = [
    % for include in sorted(data.include_dirs):
    "${include}",
    % endfor
  ]

  visibility = [
    ":*",
  ]
}

if (current_toolchain != shlib_toolchain) {

  # In the main toolchain, we just redirect to the same target in the shared
  # toolchain.

  group("${data.name}") {
    public_deps = [
      ":${data.name}($current_toolchain-shared)",
    ]

    public_configs = [
      ":${data.name}_config",
    ]
  }

} else {

  # In the shared toolchain, we normally set up the library.

  _abi_lib = "$root_out_dir/${data.lib_name}"
  % if data.has_impl_prebuilt:
  _impl_lib = "$root_out_dir/${data.lib_name}.impl"
  % endif
  _debug_lib = "$root_out_dir/lib.unstripped/${data.lib_name}"

  copy("${data.name}_copy_abi_lib") {
    sources = [
      "${data.prebuilt}",
    ]

    outputs = [
      _abi_lib,
    ]
  }

  % if data.has_impl_prebuilt:
  copy("${data.name}_copy_impl_lib") {
    sources = [
      "${data.impl_prebuilt}",
    ]

    outputs = [
      _impl_lib,
    ]
  }
  % else:
  group("${data.name}_copy_impl_lib") {}
  % endif

  copy("${data.name}_copy_unstripped_lib") {
    sources = [
      "${data.debug_prebuilt}",
    ]

    outputs = [
      _debug_lib,
    ]
  }

  _linked_lib = _abi_lib
  if (is_debug) {
    _linked_lib = _debug_lib
  }
  config("${data.name}_lib_config") {
    libs = [
      _linked_lib,
    ]

    visibility = [
      ":*",
    ]
  }

  group("${data.name}") {

    public_deps = [
      ":${data.name}_copy_abi_lib",
      ":${data.name}_copy_impl_lib",
      ":${data.name}_copy_unstripped_lib",
      % for dep in sorted(data.deps):
      "../${dep}",
      % endfor
      % for dep in sorted(data.fidl_deps):
      "../../fidl/${dep}:${dep}_c",
      % endfor
    ]

    public_configs = [
      ":${data.name}_config",
      ":${data.name}_lib_config",
    ]

    data_deps = [
      ":${data.name}_copy_abi_lib",
    ]
  }

}  # current_toolchain != shlib_toolchain

sdk_atom("${data.name}_sdk") {
  domain = "cpp"
  name = "${data.name}"
  category = "partner"

  tags = [
    "type:compiled_shared",
    "arch:target",
  ]

  shared_out_dir = get_label_info(":bogus($shlib_toolchain)", "root_out_dir")

  files = [
    % if data.with_sdk_headers:
    % for dest, source in sorted(data.includes.iteritems()):
    {
      source = "${source}"
      dest = "include/${dest}"
    },
    % endfor
    % endif
    {
      source = "$shared_out_dir/${data.lib_name}"
      dest = "lib/${data.lib_name}"
      % if not data.has_impl_prebuilt:
      packaged = true
      % endif
    },
    % if data.has_impl_prebuilt:
    {
      source = "$shared_out_dir/${data.lib_name}.impl"
      dest = "dist/${data.lib_name}"
      packaged = true
    },
    % endif
    {
      source = "$shared_out_dir/lib.unstripped/${data.lib_name}"
      dest = "debug/${data.lib_name}"
    },
  ]

  package_deps = [
    % for dep in sorted(data.deps):
    "../${dep}:${dep}_sdk",
    % endfor
  ]

  non_sdk_deps = [
    ":${data.name}",
  ]
}
