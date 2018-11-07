<%include file="header.mako" />

import("//build/banjo/banjo.gni")

banjo_dummy("${data.name}") {
  name = "${data.library}"

  sources = [
    % for source in sorted(data.sources):
    "${source}",
    % endfor
  ]
}
