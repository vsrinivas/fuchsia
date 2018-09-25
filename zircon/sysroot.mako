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

version_content = {
  root = file_base

  include_dir = "$file_base/include"
  headers = []
  % for header in sorted(data.headers):
  headers += [ "$file_base/${header}" ]
  % endfor

  link_libs = []
  % for lib in sorted(data.link_libs):
  link_libs += [ "$file_base/${lib}" ]
  % endfor

  dist_libs = []
  % for lib in sorted(data.dist_libs):
  dist_libs += [ "$file_base/${lib}" ]
  % endfor

  debug_libs = []
  % for lib in sorted(data.debug_libs):
  debug_libs += [ "$file_base/${lib}" ]
  % endfor
}
metadata = {
  type = "sysroot"
  name = "sysroot"
  root = "pkg/sysroot"
  versions = {}
  if (target_cpu == "arm64") {
    versions.arm64 = version_content
  } else if (target_cpu == "x64") {
    versions.x64 = version_content
  } else {
    assert(false, "Unknown CPU type: %target_cpu")
  }
}

sdk_atom("sysroot_sdk") {
  id = "sdk://pkg/sysroot"
  category = "partner"

  meta = {
    dest = "pkg/sysroot/meta.json"
    schema = "sysroot"
    value = metadata
  }

  files = [
    % for path, file in sorted(data.sdk_files.iteritems()):
    {
      source = "${file}"
      dest = "$file_base/${path}"
    },
    % endfor
  ]
}
