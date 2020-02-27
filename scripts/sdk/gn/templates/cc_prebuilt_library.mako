<%include file="header.mako" />

import("${data.relative_path_to_root}/build/fuchsia_sdk_pkg.gni")

fuchsia_sdk_pkg("${data.name}") {
  % if data.is_static:
  static_libs = [ "${data.name}" ]
  % else:
  shared_libs = [ "${data.name}" ]
  % endif

  deps = [
    % for dep in sorted(data.deps):
    "${dep}",
    % endfor
  ]
  sources = [
    % for header in sorted(data.hdrs):
    "${header}",
    % endfor
  ]
  include_dirs = [ "${data.includes}" ]
}

group("all"){
  deps = [
    ":${data.name}",
  ]
}
