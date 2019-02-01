# wayland_scanner

Generate a rust module from a wayland `protocol.xml` file.

# Usage
In a `BUILD.gn` file:

```
import("//build/rust/rustc_library.gni")
import("//garnet/bin/wayland/protocol.gni")

wayland_protocol("my_protocol") {
  protocol = "my_protocol.xml"
}

rustc_library("lib") {
  deps = [
    ":my_protocol",
  ]
}
```

This will generate a rust library from `my_protocol.xml` and make it available
to your library. For example usages, look at [`tests/back_end_test.rs`](tests/back_end_test.rs).
