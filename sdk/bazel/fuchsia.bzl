load("//build_defs:package.bzl", _fuchsia_package="fuchsia_package")

def fuchsia_package(name, deps, visibility=None):
  deps.append("//pkg/system")
  deps.append("//pkg/fdio")
  _fuchsia_package(name = name, deps = deps, pm = "//tools:pm", visibility = visibility)