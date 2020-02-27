<%include file="header.mako" />

import("${data.relative_path_to_root}/build/fuchsia_sdk_pkg.gni")

fuchsia_sdk_pkg("${data.name}") {
  sources = [
    % for source in sorted(data.srcs):
    "${source}",
    % endfor
    % for header in sorted(data.hdrs):
    "${header}",
    % endfor
  ]
  include_dirs = [ "${data.includes}" ]
  public_deps = [
    % for dep in sorted(data.deps):
    "${dep}",
    % endfor
  ]
}

group("all"){
  deps = [
    ":${data.name}",
  ]
}
