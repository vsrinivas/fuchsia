<%include file="header.mako" />

import("//build/cpp/verify_pragma_once.gni")
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
      % for dep in sorted(data.fidl_deps):
      "../../fidl/${dep}:${dep}_c",
      % endfor
      % for dep in sorted(data.banjo_deps):
      "../../banjo/${dep}:${dep}",
      % endfor
    ]

    public_configs = [
      ":${data.name}_config",
      ":${data.name}_lib_config",
      % for dep in sorted(data.deps):
      "../${dep}:${dep}_config",
      % endfor
    ]

    data_deps = [
      ":${data.name}_copy_abi_lib",
    ]
  }

}  # current_toolchain != shlib_toolchain

file_base = "pkg/${data.name}"
prebuilt_base = "arch/$target_cpu"
binaries_content = {
  link = "$prebuilt_base/lib/${data.lib_name}"
}
% if data.has_impl_prebuilt:
binaries_content.dist = "$prebuilt_base/dist/${data.lib_name}"
% endif
metadata = {
  name = "${data.name}"
  type = "cc_prebuilt_library"
  root = file_base
  format = "shared"
  include_dir = "$file_base/include"

  headers = []
  % if data.with_sdk_headers:
  % for dest, _ in sorted(data.includes.iteritems()):
  headers += [ "$file_base/include/${dest}" ]
  % endfor
  % endif

  binaries = {}
  if (target_cpu == "arm64") {
    binaries.arm64 = binaries_content
  } else if (target_cpu == "x64") {
    binaries.x64 = binaries_content
  } else {
    assert(false, "Unknown CPU type: $target_cpu")
  }

  deps = []
  % for dep in sorted(data.sdk_deps):
  deps += [ "${dep}" ]
  % endfor
}

shared_out_dir = get_label_info(":bogus($shlib_toolchain)", "root_out_dir")

base_meta_file = "$target_gen_dir/${data.name}.base_meta.json"
write_file(base_meta_file, metadata, "json")
augmented_meta_file = "$target_gen_dir/${data.name}.full_meta.json"
debug_mapping_file = "$target_gen_dir/${data.name}.mapping.txt"
debug_lib_file = "$shared_out_dir/lib.unstripped/${data.lib_name}"

action("${data.name}_meta") {
  script = "//build/zircon/add_library_debug_data.py"

  inputs = [
    base_meta_file,
    debug_lib_file,
  ]

  outputs = [
    augmented_meta_file,
  ]

  args = [
    "--base",
    rebase_path(base_meta_file),
    "--out",
    rebase_path(augmented_meta_file),
    "--lib-debug-file",
    rebase_path(debug_lib_file),
    "--debug-mapping",
    rebase_path(debug_mapping_file),
  ]

  deps = [
    ":${data.name}",
  ]
}

verify_pragma_once("${data.name}_pragma") {
  headers = [
    % if data.with_sdk_headers:
    % for _, source in sorted(data.includes.iteritems()):
    "${source}",
    % endfor
    % endif
  ]
}

sdk_atom("${data.name}_sdk") {
  id = "sdk://pkg/${data.name}"
  category = "partner"

  meta = {
    dest = "$file_base/meta.json"
    schema = "cc_prebuilt_library"
    source = augmented_meta_file
  }

  files = [
    % if data.with_sdk_headers:
    % for dest, source in sorted(data.includes.iteritems()):
    {
      source = "${source}"
      dest = "$file_base/include/${dest}"
    },
    % endfor
    % endif
    {
      source = "$shared_out_dir/${data.lib_name}"
      dest = "$prebuilt_base/lib/${data.lib_name}"
    },
    % if data.has_impl_prebuilt:
    {
      source = "$shared_out_dir/${data.lib_name}.impl"
      dest = "$prebuilt_base/dist/${data.lib_name}"
    },
    % endif
  ]

  file_list = debug_mapping_file

  deps = [
    % for dep in sorted(data.sdk_deps):
    "../${dep}:${dep}_sdk",
    % endfor
  ]

  non_sdk_deps = [
    ":${data.name}",
    ":${data.name}_meta",
    ":${data.name}_pragma",
  ]
}
