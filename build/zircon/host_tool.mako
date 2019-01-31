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

file_base = "tools/${data.name}"

sdk_atom("${data.name}_sdk") {
  id = "sdk://tools/${data.name}"
  category = "partner"

  meta = {
    dest = "$file_base-meta.json"
    schema = "host_tool"
    value = {
      type = "host_tool"
      name = "${data.name}"
      root = "tools"
      files = [
        file_base,
      ]
    }
  }

  files = [
    {
      source = "${data.executable}"
      dest = file_base
    }
  ]
}
