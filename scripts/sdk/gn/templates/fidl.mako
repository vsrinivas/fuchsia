<%include file="header.mako" />

import("${data.relative_path_to_root}/build/fuchsia_sdk_pkg.gni")

fuchsia_sdk_fidl_pkg("${data.name}") {
  package_name = "${data.short_name}"
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
