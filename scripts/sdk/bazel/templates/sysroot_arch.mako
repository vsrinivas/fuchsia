<%include file="header.mako" />

load("//build_defs:package_files.bzl", "package_files")

exports_files(
    glob(["**"]),
)

package_files(
    name = "dist",
    contents = {
        % for path, source in sorted(data.packaged_files.iteritems()):
        "${source}": "${path}",
        % endfor
    },
    visibility = [
        "//visibility:public",
    ],
)
