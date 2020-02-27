<%include file="header.mako" />

import("${data.relative_path_to_root}/build/fidl_library.gni")

fidl_library("${data.name}") {
  library_name = "${data.short_name}"
  namespace = "${data.namespace}"
  public_deps = [
    % for dep in sorted(data.deps):
    "../${dep}",
    % endfor
  ]
  sources = [
    % for source in sorted(data.srcs):
    "${source}",
    % endfor
  ]
}

group("all"){
  deps = [
    ":${data.name}",
  ]
}
