<%include file="header.mako" />

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

config("${data.name}_config") {
  include_dirs = [
    % for include in sorted(data.include_dirs):
    "${include}",
    % endfor
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
