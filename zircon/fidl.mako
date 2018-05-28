<%include file="header.mako" />

import("//build/fidl/fidl.gni")

fidl("${data.name}") {
  name = "${data.library}"

  sdk_category = "partner"

  sources = [
    % for source in sorted(data.sources):
    "${source}",
    % endfor
  ]

  deps = [
    % for dep in sorted(data.fidl_deps):
    "../${dep}",
    % endfor
  ]
}
