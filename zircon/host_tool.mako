<%include file="header.mako" />

import("//build/gn/config.gni")
import("//build/sdk/sdk_atom.gni")

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

sdk_atom("${data.name}_sdk") {
  domain = "exe"
  name = "${data.name}"

  tags = [
    "arch:host",
  ]

  files = [
    {
      source = "${data.executable}"
      dest = "${data.name}"
    },
  ]
}
