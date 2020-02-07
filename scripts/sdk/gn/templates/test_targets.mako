<%include file="header.mako" />

assert(current_os == "fuchsia")

import("fuchsia_sdk_pkg.gni")

# This template is used to create build targets
# that test the generated build targets. It does not
# have any practical use outside testing.
template("fuchsia_sdk_test_targets") {
  not_needed(["invoker"])
  group(target_name) {
    deps = [
    % for dep in sorted(data.fidl_targets):
      "<%text>${fuchsia_sdk}</%text>/sdk/fidl/${dep}",
    % endfor
    ]
  }
}