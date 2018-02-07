<%include file="header.mako" />

import("//build/gn/config.gni")
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

if (!is_pic_default) {

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

_lib = "$root_out_dir/${data.lib_name}"
_debug_lib = "$root_out_dir/lib.unstripped/${data.lib_name}"

copy("${data.name}_copy_lib") {
  sources = [
    "${data.prebuilt}",
  ]

  outputs = [
    _lib,
  ]
}

copy("${data.name}_copy_unstripped_lib") {
  sources = [
    "${data.debug_prebuilt}",
  ]

  outputs = [
    _debug_lib,
  ]
}

_linked_lib = _lib
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
    ":${data.name}_copy_lib",
    ":${data.name}_copy_unstripped_lib",
    % for dep in sorted(data.deps):
    "../${dep}",
    % endfor
  ]

  public_configs = [
    ":${data.name}_config",
    ":${data.name}_lib_config",
  ]
}

}  # !is_pic_default

sdk_atom("${data.name}_sdk") {
  domain = "c-pp"
  name = "${data.name}"

  tags = [
    "type:compiled_shared",
  ]

  files = [
    % for dest, source in sorted(data.includes.iteritems()):
    {
      source = "${source}"
      dest = "include/${dest}"
    },
    % endfor
    {
      source = "${data.prebuilt}"
      dest = "lib/${data.lib_name}"
    },
    {
      source = "${data.debug_prebuilt}"
      dest = "debug/${data.lib_name}"
    },
  ]
}
