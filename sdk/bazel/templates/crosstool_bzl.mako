<%include file="header_no_license.mako" />

"""
Defines a Fuchsia crosstool workspace.
"""

# TODO(alainv): Do not hardcode download URLs but export the URL from the
#               the one used in //buildtools, using the CIPD APIs.
CLANG_LINUX_DOWNLOAD_URL = (
    "https://storage.googleapis.com/fuchsia/clang/linux-amd64/0c20cca57e424c54cd65644168c68725aae41c44"
)
CLANG_LINUX_SHA256 = (
    "fa304a74a9e39d1e6d4cdf29d368923abc58fa974d1ecb55662065252c4a1802"
)

CLANG_MAC_DOWNLOAD_URL = (
    "https://storage.googleapis.com/fuchsia/clang/mac-amd64/21371c6cb01e83f8c1657af5f03ce0ff16c1fa0e"
)
CLANG_MAC_SHA256 = (
    "3049d5ad7d8dc3f6e644c96ba206ccd16ef6a4ee7288d913a12bda8f40f9afed"
)


def _configure_crosstool_impl(repository_ctx):
    """
    Configures the Fuchsia crosstool repository.
    """
    if repository_ctx.os.name == "linux":
      clang_download_url = CLANG_LINUX_DOWNLOAD_URL
      clang_sha256 = CLANG_LINUX_SHA256
    elif repository_ctx.os.name == "mac os x":
      clang_download_url = CLANG_MAC_DOWNLOAD_URL
      clang_sha256 = CLANG_MAC_SHA256
    else:
      fail("Unsupported platform: %s" % repository_ctx.os.name)

    # Download the toolchain.
    repository_ctx.download_and_extract(
        url = clang_download_url,
        output = "clang",
        sha256 = clang_sha256,
        type = "zip",
    )
    # Set up the BUILD file from the Fuchsia SDK.
    repository_ctx.symlink(
        Label("@fuchsia_sdk//build_defs:BUILD.crosstool"),
        "BUILD",
    )
    # Hack to get the path to the sysroot directory, see
    # https://github.com/bazelbuild/bazel/issues/3901
    % for arch in data.arches:
    sysroot_${arch.short_name} = repository_ctx.path(
        Label("@fuchsia_sdk//arch/${arch.short_name}/sysroot:BUILD")).dirname
    % endfor
    # Set up the CROSSTOOL file from the template.
    repository_ctx.template(
        "CROSSTOOL",
        Label("@fuchsia_sdk//build_defs:CROSSTOOL.in"),
        substitutions = {
            % for arch in data.arches:
            "%{SYSROOT_${arch.short_name.upper()}}": str(sysroot_${arch.short_name}),
            % endfor
            "%{CROSSTOOL_ROOT}": str(repository_ctx.path("."))
        },
    )


install_fuchsia_crosstool = repository_rule(
    implementation = _configure_crosstool_impl,
)
