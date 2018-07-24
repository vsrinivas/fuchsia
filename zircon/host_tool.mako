<%include file="header.mako" />

import("//build/sdk/sdk_atom.gni")

assert(current_toolchain == host_toolchain,
       "The ${data.name} tool is host-only.")

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
  id = "sdk://tools/${data.name}"
  category = "partner"

  tags = [ "arch:host" ]

  files = [
    {
      source = "${data.executable}"
      dest = "${data.name}"
    },
  ]
}
