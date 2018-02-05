<%include file="header.mako" />

import("//build/gn/config.gni")

if (current_toolchain != host_toolchain) {
  assert(false, "The ${data.name} tool is host-only.")
}

copy("${data.name}") {
  sources = [
    "${data.executable}",
  ]

  outputs = [
    "$root_out_dir/${data.name}",
  ]
}
