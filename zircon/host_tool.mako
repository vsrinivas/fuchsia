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

meta_file = "$target_gen_dir/${data.name}.sdk_meta.json"
meta_data = {
  type = "host_tool"
  name = "${data.name}"
  files = [
    "tools/${data.name}",
  ]
}
write_file(meta_file, meta_data, "json")

sdk_atom("${data.name}_sdk") {
  domain = "exe"
  name = "${data.name}"
  id = "sdk://tools/${data.name}"
  category = "partner"

  meta = {
    source = meta_file
    dest = "tools/${data.name}-meta.json"
    schema = "host_tool"
  }

  tags = [ "arch:host" ]

  files = [
    {
      source = "${data.executable}"
      dest = "${data.name}"
    },
  ]
}
