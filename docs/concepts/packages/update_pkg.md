# The update package

The update package is a package containing files and rules for how to update the
system.

## System update

The system update checker looks at the merkle root of the system image that the update
package has and compares it to the merkle root of the running system. It also checks
the merkle root of the update package and compares it to the version that the system
update checker last used. If they're different, then something other than the
system updater has updated the system.

The system updater reboots the device after a successful system update.

The system update checker periodically fetches the update package using the package
resolver and sees if it looks different. If the update package is different,
the system triggers a package update.

The system updater is designed such that the process can be interrupted at
any time and it does not leave the system in an unbootable or corrupt state.

First, the system updater reads the `update_mode` file to determine what operations to
perform. Then, the board file reads and verifies that there is no misconfigurations
Then, the update package fetches the packages to serve. Finally, the update package writes
the kernel images and ensures that `vbmeta` must be written after the kernel image.


## Content of the update package

The structure of the update package--fuchsia-pkg://fuchsia.com/update--looks like:

*   `/board`
    The board name. The updater verifies the contents and does an update if they match
    the previous board name to prevent accidentally attempting to update a device to an
    unsupported architecture.  For example, attempting to update an x64 target to an arm64 build will fail.

*   `/bootloader`
    Image of the bootloader firmware.

*   `/packages`
    Line separated list of the packages to install of the form “package-name/0=hash”.
    Deprecated in favor of `packages.json`, if that file is present.
    For example:
        ```
        component_index/0=40da91deffd7531391dd067ed89a19703a73d4fdf19fe72651ff30e414c4ef0a
        system_image/0=c391b60a35f680b1cf99107309ded12a8219aedb4d296b7fa8a9c5e95ade5e85
        ```
*   `/packages.json`
    Json formatted list of merkle pinned package urls that belong to the base package set
    of the target OS image. The update package looks at either the `/packages` file or 
    `/packages.json` to determine what (and in what order) needs to be updated, giving priority
    to `packages.json`, if it is present.
    For example:
        ```
        {
	        “version”: “1”,
	        “content”: [
                "fuchsia-pkg://fuchsia.com/component_index/0?hash=40da91deffd7531391dd067ed89a19703a73d4fdf19fe72651ff30e414c4ef0a",
                "fuchsia-pkg://fuchsia.com/system_image/0?hash=c391b60a35f680b1cf99107309ded12a8219aedb4d296b7fa8a9c5e95ade5e85"
            ]
        }
        ```
*   `/version`
    Same format as the [`/config/build-info/version`](/docs/development/build/build_information.md) file.
*   `/zbi\[.signed\]`
    Kernel image. Must not be present if the update-mode is ForceRecovery. zbi or zbi.signed
    is required to be present if the update-mode is Normal.
*   `/zedboot\[.signed\]`
    Recovery image
*   `/meta/contents` + `/meta/package`
    Metadata files present in all packages
*   `update_mode.json`
    Optional. If the file is not present, the update mode is normal. The other option is
    ForceRecovery which writes a recovery image and reboots into it. Any other update-mode
    is invalid.
    For example:
        ```
        { version: "1", content: { "mode" : "force-recovery" } }
        ```