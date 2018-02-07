<%include file="header.mako" />

import("//build/cpp/sdk_source_set.gni")

config("${data.name}_config") {
  include_dirs = [
    % for include in sorted(data.include_dirs):
    "${include}",
    % endfor
  ]

  visibility = [
    ":*",
  ]
}

sdk_source_set("${data.name}") {

  sources = [
    % for source in sorted(data.sources):
    "${source}",
    % endfor
  ]

  public_deps = [
    % for dep in sorted(data.deps):
    "../${dep}",
    % endfor
  ]

  libs = [
    % for lib in sorted(data.libs):
    "${lib}",
    % endfor
  ]

  public_configs = [
    ":${data.name}_config",
  ]

  defines = [
    "_ALL_SOURCE=1",
  ]
}
