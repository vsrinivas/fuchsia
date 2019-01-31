<%include file="header_no_license.mako" />

load("@io_bazel_rules_dart//dart/build_rules/internal:pub.bzl", "pub_repository")

def setup_dart():
    % if data:
      % for name, version in data.iteritems():
    pub_repository(
        name = "vendor_${name}",
        output = ".",
        package = "${name}",
        version = "${version}",
        pub_deps = [],
    )
      % endfor
    % else:
    pass
    % endif
