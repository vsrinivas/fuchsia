<%include file="header_no_license.mako" />

"""
Defines a Fuchsia crosstool workspace.
"""

# TODO(alainv): Do not hardcode download URLs but export the URL from the
#               the one used in //buildtools, using the CIPD APIs.
CLANG_DOWNLOAD_URL = (
    "https://storage.googleapis.com/fuchsia/clang/linux-amd64/0c20cca57e424c54cd65644168c68725aae41c44"
)

CLANG_SHA256 = (
    "fa304a74a9e39d1e6d4cdf29d368923abc58fa974d1ecb55662065252c4a1802"
)


def _configure_crosstool_impl(repository_ctx):
    """
    Configures the Fuchsia crosstool repository.
    """
    # Download the toolchain.
    repository_ctx.download_and_extract(
        url = CLANG_DOWNLOAD_URL,
        output = "clang",
        sha256 = CLANG_SHA256,
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
