<%include file="header.mako" />

import("//build/sdk/sdk_atom.gni")

% if data.is_shared:
_dest_dir = "root_out_dir"
% else:
_dest_dir = "target_out_dir"
% endif
_out_dir = get_label_info(":bogus($shlib_toolchain)", _dest_dir)

copy("${data.name}_copy_lib") {
  sources = [
    "${data.prebuilt}",
  ]

  outputs = [
    "$_out_dir/${data.lib_name}",
  ]
}

% if data.is_shared:
copy("${data.name}_copy_unstripped_lib") {
  sources = [
    "${data.debug_prebuilt}",
  ]

  outputs = [
    "$_out_dir/lib.unstripped/${data.lib_name}",
  ]
}
% endif

linked_lib = "$_out_dir/${data.lib_name}"
% if data.is_shared:
if (is_debug) {
  linked_lib = "$_out_dir/lib.unstripped/${data.lib_name}"
}
% endif

config("${data.name}_config") {
  include_dirs = [
    % for include in sorted(data.include_dirs):
    "${include}",
    % endfor
  ]

  libs = [
    linked_lib,
  ]
}

group("${data.name}") {

  deps = [
    ":${data.name}_copy_lib",
    % if data.is_shared:
    ":${data.name}_copy_unstripped_lib",
    % endif
    % for dep in sorted(data.deps):
    "../${dep}",
    % endfor
  ]

  public_configs = [
    ":${data.name}_config",
  ]
}

sdk_atom("${data.name}_sdk") {
  domain = "c-pp"
  name = "${data.name}"

  % if data.is_shared:
  prefix = "shared"
  % else:
  prefix = "static"
  % endif
  tags = [
    "type:compiled_$prefix",
  ]

  files = [
    % for dest, source in sorted(data.includes.iteritems()):
    {
      source = "${source}"
      dest = "${dest}"
    },
    % endfor
    {
      source = "${data.prebuilt}"
      dest = "${data.lib_name}"
    },
    % if data.is_shared:
    {
      source = "${data.debug_prebuilt}"
      dest = "debug/${data.lib_name}"
    },
    % endif
  ]
}
