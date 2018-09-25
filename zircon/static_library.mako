<%include file="header.mako" />

import("//build/sdk/sdk_atom.gni")

_lib = "$target_out_dir/${data.lib_name}"

copy("${data.name}_copy_lib") {
  sources = [
    "${data.prebuilt}",
  ]

  outputs = [
    _lib,
  ]
}

config("${data.name}_config") {
  include_dirs = [
    % for include in sorted(data.include_dirs):
    "${include}",
    % endfor
  ]

  libs = [
    _lib,
  ]

  visibility = [
    ":*",
  ]
}

group("${data.name}") {

  public_deps = [
    ":${data.name}_copy_lib",
    % for dep in sorted(data.deps):
    "../${dep}",
    % endfor
    % for dep in sorted(data.fidl_deps):
    "../../fidl/${dep}:${dep}_c",
    % endfor
  ]

  public_configs = [
    ":${data.name}_config",
  ]
}

file_base = "pkg/${data.name}"
prebuilt_base = "arch/$target_cpu"
binaries_content = {
  link = "$prebuilt_base/lib/${data.lib_name}"
}
metadata = {
  name = "${data.name}"
  type = "cc_prebuilt_library"
  root = file_base
  format = "static"
  include_dir = "$file_base/include"

  headers = []
  % for dest, _ in sorted(data.includes.iteritems()):
  headers += [ "$file_base/include/${dest}" ]
  % endfor

  binaries = {}
  if (target_cpu == "arm64") {
    binaries.arm64 = binaries_content
  } else if (target_cpu == "x64") {
    binaries.x64 = binaries_content
  } else {
    assert(false, "Unknown CPU type: %target_cpu")
  }

  deps = []
  % for dep in sorted(data.deps):
  deps += [ "${dep}" ]
  % endfor
}

sdk_atom("${data.name}_sdk") {
  id = "sdk://pkg/${data.name}"
  category = "partner"

  meta = {
    dest = "$file_base/meta.json"
    schema = "cc_prebuilt_library"
    value = metadata
  }

  files = [
    % for dest, source in sorted(data.includes.iteritems()):
    {
      source = "${source}"
      dest = "$file_base/include/${dest}"
    },
    % endfor
    {
      source = _lib
      dest = "$prebuilt_base/lib/${data.lib_name}"
    },
  ]

  deps = [
    % for dep in sorted(data.deps):
    "../${dep}:${dep}_sdk",
    % endfor
  ]

  non_sdk_deps = [
    ":${data.name}",
  ]
}
