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

sdk_atom("sysroot_sdk") {
  domain = "cpp"
  name = "system"
  category = "partner"

  tags = [
    "type:sysroot",
  ]

  files = [
    % for path, file in sorted(data.files.iteritems()):
    {
      source = "${file}"
      dest = "${path}"
    },
    % endfor
  ]
}
