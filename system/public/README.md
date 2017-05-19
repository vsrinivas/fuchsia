## Magenta Public Headers

This directory is in the include path for all kernel and system
modules in Magenta, and is also exported into the include directory
of the generate sysroot.

Most headers should live with their respective libraries.  These
headers are ones that need to be global because they're a public
API/ABI surface or a header needed by a large number of public
headers (eg, magenta/compiler.h)

Headers in this directory are also used building Fuchsia host tools.
These builds cannot get these headers from sysroot: sysroot is for target
builds. Instead such tools need to add the following to their BUILD.gn:

```
executable("foo") {
  deps = [ ... ]
  if (!is_fuchsia) {
    deps += [ "//magenta/system/public" ]
  }
}
```
