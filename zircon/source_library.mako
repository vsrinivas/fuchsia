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

source_set("${data.name}") {
  sources = [
    % for _, source in sorted(data.sources.iteritems()):
    "${source}",
    % endfor
    % for _, source in sorted(data.includes.iteritems()):
    "${source}",
    % endfor
  ]

  public_deps = [
    % for dep in sorted(data.deps):
    "../${dep}",
    % endfor
    % for dep in sorted(data.fidl_deps):
    "../../fidl/${dep}:${dep}_c",
    % endfor
  ]

  libs = [
    % for lib in sorted(data.libs):
    "${lib}",
    % endfor
  ]

  public_configs = [
    ":${data.name}_config",
  ]
}

file_base = "pkg/${data.name}"
metadata = {
  name = "${data.name}"
  type = "cc_source_library"
  root = file_base
  include_dir = "$file_base/include"

  sources = []
  % for dest, _ in sorted(data.sources.iteritems()):
  sources += [ "$file_base/${dest}" ]
  % endfor

  headers = []
  % for dest, _ in sorted(data.includes.iteritems()):
  headers += [ "$file_base/include/${dest}" ]
  % endfor

  deps = []
  % for dep in sorted(data.deps):
  deps += [ "${dep}" ]
  % endfor

  fidl_deps = []

  files = sources + headers
}

sdk_atom("${data.name}_sdk") {
  domain = "cpp"
  name = "${data.name}"
  id = "sdk://pkg/${data.name}"
  category = "partner"

  meta = {
    dest = "$file_base/meta.json"
    schema = "cc_source_library"
    value = metadata
  }

  tags = [
    "type:sources",
  ]

  files = [
    % for dest, source in sorted(data.includes.iteritems()):
    {
      source = "${source}"
      dest = "include/${dest}"
    },
    % endfor
    % for dest, source in sorted(data.sources.iteritems()):
    {
      source = "${source}"
      dest = "${dest}"
    },
    % endfor
  ]

  new_files = [
    % for dest, source in sorted(data.includes.iteritems()):
    {
      source = "${source}"
      dest = "$file_base/include/${dest}"
    },
    % endfor
    % for dest, source in sorted(data.sources.iteritems()):
    {
      source = "${source}"
      dest = "$file_base/${dest}"
    },
    % endfor
  ]

  package_deps = [
    % for dep in sorted(data.deps):
    "../${dep}:${dep}_sdk",
    % endfor
  ]
}
