<%include file="header.mako" />

copy("${data.name}_copy_lib") {
  out_dir = get_label_info(":bogus($shlib_toolchain)", "target_out_dir")

  sources = [
    "${data.prebuilt}",
  ]

  outputs = [
    "$out_dir/{{source_file_part}}",
  ]
}

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
    % for dep in sorted(data.deps):
    "../${dep}",
    % endfor
  ]

  public_configs = [
    ":${data.name}_config",
  ]
}
