<%include file="header.mako" />
package(default_visibility = ["//visibility:public"])

% for arch in sorted(data.arches):
filegroup(
    name = "${arch}",
    srcs = glob(["${arch}/*"]),
)

% endfor