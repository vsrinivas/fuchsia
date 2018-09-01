<%include file="header.mako" />

import("//build/sdk/sdk_atom.gni")

_out_dir = get_label_info(":bogus", "target_out_dir")

<%def name="copy_target(path)">${"copy_%s" % path.replace('/', '_').replace('.', '_')}</%def>

% for path, file in sorted(data.files.iteritems()):
copy("${copy_target(path)}") {
  sources = [
    "${file}",
  ]

  outputs = [
    "$_out_dir/sysroot/${path}",
  ]
}

% endfor

group("sysroot") {
  deps = [
    % for path, file in sorted(data.files.iteritems()):
    ":${copy_target(path)}",
    % endfor
  ]
}

file_base = "arch/$target_cpu/sysroot"

metadata = {
  type = "sysroot"
  name = "sysroot"
  root = "pkg/sysroot"
  files = [
    % for path, _ in sorted(data.sdk_files.iteritems()):
    "$file_base/${path}",
    % endfor
  ]
}

sdk_atom("sysroot_sdk") {
  domain = "cpp"
  name = "system"
  id = "sdk://pkg/sysroot"
  category = "partner"

  meta = {
    dest = "pkg/sysroot/meta.json"
    schema = "sysroot"
    value = metadata
  }

  tags = [
    "type:sysroot",
  ]

  files = [
    % for path, file in sorted(data.sdk_files.iteritems()):
    {
      source = "${file}"
      dest = "${path}"
      % if path.startswith("dist/"):
      packaged = true
      % endif
    },
    % endfor
  ]

  new_files = [
    % for path, file in sorted(data.sdk_files.iteritems()):
    {
      source = "${file}"
      dest = "$file_base/${path}"
    },
    % endfor
  ]
}
