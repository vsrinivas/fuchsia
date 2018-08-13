<%include file="header_no_license.mako" />

"""
Defines a Fuchsia crosstool workspace.
"""

# TODO(alainv): Do not hardcode download URLs but export the URL from the
#               the one used in //buildtools, using the CIPD APIs.
CLANG_LINUX_DOWNLOAD_URL = (
    "https://storage.googleapis.com/fuchsia/clang/linux-amd64/2a605accf10c22e7905d2cabec22ca317869f85d"
)
CLANG_LINUX_SHA256 = (
    "776b8b7b47da73199f095fc0cabca85e9d9ced7e3bbfc7707e0a4afaacc6544b"
)

CLANG_MAC_DOWNLOAD_URL = (
    "https://storage.googleapis.com/fuchsia/clang/mac-amd64/c0cea0a0fa8cff6d286e46aa4530ffc6b85baaaf"
)
CLANG_MAC_SHA256 = (
    "f06e6cf9bcb09963a3042ff8c8bbfe998f43bf4d61dc5bbb1f1c496792d6bea2"
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
