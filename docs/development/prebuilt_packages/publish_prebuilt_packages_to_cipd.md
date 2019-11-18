# Publish prebuilt packages to CIPD

To integrate your software into the Fuchsia project as prebuilt packages,
you need to publish your prebuilt packages to
Chrome Infrastructure Package Deployment
([CIPD](https://github.com/luci/luci-go/tree/master/cipd){: .external}).
A prebuilt package is a Fuchsia archive
([FAR](/docs/concepts/storage/archive_format.md)) file that
contains the binaries and metadata of your software.

Once you set up continuous integration (CI) with Fuchsia,
whenever you publish new versions of the prebuilt packages to CIPD,
Fuchsia’s CI system fetches those new packages and
roll them into Fuchsia through
the [global integration](https://fuchsia.googlesource.com/integration/+/refs/heads/master)
process.

## Prerequisite

Before you start working on publishing prebuilt packages,
you need to know how to
[build a prebuilt package](/docs/development/sdk/documentation/packages.md#build-package).

## A CIPD package {#a-cipd-package}

The main purpose of publishing prebuilt packages to CIPD is
for Fuchsia’s CI system to fetch your most recent prebuilt packages
from CIPD for global integration.

Note: CIPD is not a package repository for Fuchsia devices.
A running Fuchsia device doesn't install prebuilt packages from CIPD.

Both Fuchsia and CIPD have the notion of a package.
The differences between a prebuilt package and a CIPD package are:

*   A prebuilt package - A Fuchsia archive (FAR) file that
    contains the binaries and metadata of your software.
*   A CIPD package - An archive that contains
    one or more Fuchsia’s prebuilt packages and other relevant files.

Updating the content of a CIPD package creates a new instance of
the CIPD package. Every CIPD package maintains the history of its instances
(see [Figure 1](#figure-1) below).

## Publish your prebuilt packages to CIPD {#publish-your-prebuilt-packages-to-cipd}

To publish your prebuilt packages to CIPD,
see [Publish a CIPD package](#publish-a-cipd-package) below.

Additionally, if your CIPD package contains
[ELF binaries](https://en.wikipedia.org/wiki/Executable_and_Linkable_Format){: .external}, see
[Publish a CIPD package with unstripped ELF binaries](#publish-a-cipd-package-with-unstripped-elf-binaries)
below.

### Publish a CIPD package {#publish-a-cipd-package}

Fuchsia has the following requirements for a CIPD package:

*   Use the following naming convention:

    ```
    <PROJECT>/fuchsia/<PACKAGE>-<ARCHITECTURE>
    ```
    For example,
    [chromium/fuchsia/webrunner-arm64](https://chrome-infra-packages.appspot.com/p/chromium/fuchsia/webrunner-arm64/+/){: .external}
    and
    [chromium/fuchsia/castrunner-amd64](https://chrome-infra-packages.appspot.com/p/chromium/fuchsia/castrunner-amd64/+/){: .external}.
*   Include a `LICENSE` file that contains the legal notices of the software.
*   Provide a Fuchsia archive (FAR) file per prebuilt package.
    For example, `chromium.far`, `webrunner.far`.
*   [Tag](https://github.com/luci/luci-go/tree/master/cipd#tags){: .external}
    each instance with a version identifier in the form of:

    ```
    version:<VERSION_ID_OF_INSTANCE>
    ```
    For example, `version:77.0.3835.0` and `version:176326.`
*   Create a [ref](https://github.com/luci/luci-go/tree/master/cipd#refs){: .external}
    labeled `latest` and point it to the most recent instance of your CIPD package.

Fuchsia developers need to be able to identify which source code is used to
generate an instance of the CIPD package
based on the version identifier of the instance.
Fuchsia recommends that in your project’s documentation
you provide instructions on how to obtain an instance’s source code.

When you publish a new instance of your CIPD package,
you need to update the `latest` ref so that it now points to the new instance.
Fuchsia’s CI system monitors your package’s `latest` ref;
when the CI system detects that the `latest` ref is updated,
it fetches the new package and rolls it into Fuchsia.

<a name="figure-1"></a>
<figure>
  <img src="/docs/images/development/prebuilt_packages/publish-prebuilt-packages-to-fuchsia-00.png"
       alt="The latest ref and other refs shown in the CIPD UI">
  <figcaption><b>Figure 1</b>. The CIPD UI shows
  the latest ref and other refs used for this CIPD package instances.</figcaption>
</figure>

The following example shows the content of a CIPD package:

```
LICENSE
chromium.far
webrunner.far
```

### Publish a CIPD package with unstripped ELF binaries {#publish-a-cipd-package-with-unstripped-elf-binaries}

If your CIPD package contains
[ELF binaries](https://en.wikipedia.org/wiki/Executable_and_Linkable_Format){: .external}
(executables and shared libraries),
you need to publish a CIPD package that contains the unstripped versions of those ELF binaries.
The unstripped ELF binaries allow Fuchsia developers to debug your software.
For example, the unstripped ELF binaries enable symbolizing stack traces.

Typically the owner of prebuilt packages publishes
a sibling CIPD package for the unstripped ELF binaries per architecture,
in addition to publishing a CIPD package that contains the prebuilt packages.
To allow these CIPD packages to be rolled together,
the instances of these CIPD packages must share the same version identifier
[tag](https://github.com/luci/luci-go/tree/master/cipd#tags){: .external}.

Fuchsia requires a CIPD package with unstripped ELF binaries
to have the following directory structure:

*   An instance of a CIPD package contains a `.tar.bz2` file per prebuilt package.
    *   Each `.tar.bz2` file, once uncompressed and unpacked, is a `.build-id` directory.

Note: Don't include a directory named `.build-id` as a root directory in the
`.tar.bz2` file. The unpacked content of the `.tar.bz2`
file is a collection of subdirectories (see the example below).

*   A `.build-id` directory has subdirectories and
    the subdirectories contain unstripped ELF binaries.
    *   Each subdirectory represents
        the first two characters of ELF binaries’ `build-id` (see the example below).

Fuchsia requires the following requirements for an unstripped ELF binary:

*   Use the `debug` extension, which indicates that the binary contains
    DWARF debug information.
*   Use the first two characters of the `build-id` for the name of
    the subdirectory and the rest of the `build-id` for its filename.
    For example, if `build-id` is `1dbca0bd1be33e19`,
    then its subdirectory name is `1d` and
    its unstripped ELF binary filename is `bca0bd1be33e19.debug`.
*   Include the following information:
    *   A `NT_GNU_BUILD_ID` note
        (obtained by passing the `-build-id` flag to the linker).
    *   Debug information
        (obtained by passing the `-g` flag to the compiler).

This example shows the directory structure of a CIPD package
with unstripped ELF binaries:

```none
chromium.symbols.tar.bz2
   1d/
      bca0bd1be33e19.debug
   2b/
      0e519bcf3942dd.debug
   5b/
      66bc85af2da641697328996cbc04d62b84fc58.debug

webrunner.symbols.tar.bz2
   1f/
      512abdcbe453ee.debug
      90dd45623deab1.debug
   3d/
      aca0b11beff127.debug
   5b/
      66bc85af2da641697328996cbc04d62b84fc58.debug
```
