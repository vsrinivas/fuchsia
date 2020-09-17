# GN Build Arguments

## All builds

### active_partition

**Current value (from the default):** `""`

From //build/images/args.gni:84

### add_qemu_to_build_archives
Whether to include images necessary to run Fuchsia in QEMU in build
archives.

**Current value (from the default):** `false`

From //build/images/args.gni:90

### additional_bootserver_arguments
Additional bootserver args to add to pave.sh. New uses of this should be
added with caution, and ideally discussion. The present use case is to
enable throttling of netboot when specific network adapters are combined
with specific boards, due to driver and hardware challenges.

**Current value (from the default):** `""`

From //build/images/args.gni:96

### all_font_file_paths
List of file paths to every font asset. Populated in fonts.gni.

**Current value (from the default):** `[]`

From //src/fonts/build/font_args.gni:35

### all_toolchain_variants
*These should never be set as a build argument.*
It will be set below and passed to other toolchains through toolchain_args
(see variant_toolchain.gni).

**Current value (from the default):** `[]`

From //build/config/BUILDCONFIG.gn:1101

### always_zedboot
Build boot images that prefer Zedboot over local boot (only for EFI).

**Current value (from the default):** `false`

From //build/images/args.gni:108

### asan_default_options
Default [AddressSanitizer](https://clang.llvm.org/docs/AddressSanitizer.html)
options (before the `ASAN_OPTIONS` environment variable is read at
runtime).  This can be set as a build argument to affect most "asan"
variants in $variants (which see), or overridden in $toolchain_args in
one of those variants.  This can be a list of strings or a single string.

Note that even if this is empty, programs in this build **cannot** define
their own `__asan_default_options` C function.  Instead, they can use a
sanitizer_extra_options() target in their `deps` and then any options
injected that way can override that option's setting in this list.

**Current value (from the default):** `[]`

From //zircon/public/gn/config/instrumentation/sanitizer_default_options.gni:16

### auto_login_to_guest
Whether basemgr should use a random identifier for sessions, leading to
a different persistent data location for every session.

**Current value (from the default):** `false`

From //src/modular/bin/basemgr/BUILD.gn:15

### auto_update_packages
Whether the component loader should automatically update packages.

**Current value (from the default):** `true`

From //src/sys/sysmgr/BUILD.gn:10

### avb_algorithm
AVB algorithm type.Supported options:
  SHA256_RSA2048
  SHA256_RSA4096
  SHA256_RSA8192
  SHA512_RSA2048
  SHA512_RSA4096
  SHA512_RSA8192

**Current value (from the default):** `"SHA512_RSA4096"`

From //build/images/vbmeta.gni:33

### avb_atx_metadata
AVB metadata which will be used to validate public key

**Current value (from the default):** `""`

From //build/images/vbmeta.gni:24

### avb_key
a key which will be used to sign VBMETA and images for AVB

**Current value (from the default):** `""`

From //build/images/vbmeta.gni:21

### base_cache_packages_allow_testonly
Whether to allow testonly=true targets in base/cache pacakges. Default to
true to allow testonly=true targets. It is preferrable to set to false for
production builds to avoid accidental inclusion of testing targets.

**Current value (from the default):** `true`

From //BUILD.gn:85

### base_package_labels
If you add package labels to this variable, the packages will be included in
the 'base' package set, which represents the set of packages that are part
of an OTA. These pacakages are updated as an atomic unit during an OTA
process and are immutable and are a superset of the TCB (Trusted Computing
Base) for a product. These packages are never evicted by the system.

**Current value for `target_cpu = "arm64"`:** `["//build/info:build-info", "//garnet/bin/log_listener:log_listener", "//garnet/bin/log_listener:log_listener_shell", "//garnet/bin/setui:setui_service", "//garnet/bin/sshd-host", "//garnet/bin/sshd-host:config", "//garnet/bin/sysmgr", "//garnet/bin/sysmgr:network_config", "//garnet/bin/sysmgr:services_config", "//garnet/bin/timezone", "//src/cobalt/bin/app:cobalt", "//src/cobalt/bin/app:cobalt_registry", "//src/cobalt/bin/app:config", "//src/cobalt/bin/system-metrics:cobalt_system_metrics", "//src/cobalt/bin/system-metrics:config", "//src/connectivity/bluetooth:core", "//src/connectivity/management/reachability", "//src/connectivity/management/reachability:reachability_sysmgr_config", "//src/connectivity/management:network_config_default", "//src/connectivity/network/mdns/bundles:config", "//src/connectivity/network/mdns/bundles:services", "//src/connectivity/network:config", "//src/connectivity/wlan:packages", "//src/connectivity/wlan/config:default", "//src/developer/forensics:pkg", "//src/developer/forensics/snapshot:pkg", "//src/diagnostics/archivist", "//src/diagnostics/archivist:with_default_config", "//src/hwinfo:hwinfo", "//src/hwinfo:default_product_config", "//src/media/audio/bundles:audio_config", "//src/recovery/factory_reset", "//src/security/policy:appmgr_policy_eng", "//src/security/root_ssl_certificates", "//src/sys/appmgr", "//src/sys/appmgr:appmgr_scheme_config", "//src/sys/appmgr:core_component_id_index", "//src/sys/core", "//src/sys/device_settings:device_settings_manager", "//src/sys/pkg:core", "//src/sys/pkg:pkgfs-disable-executability-restrictions", "//src/sys/pkg:system-update-checker", "//src/sys/pkg/bin/pkg-resolver:enable_dynamic_configuration", "//src/sys/stash:pkg", "//src/sys/time/network_time_service:network-time-service", "//src/sys/time/timekeeper", "//third_party/openssh-portable/fuchsia/developer-keys:ssh_config", "//src/sys/pkg:tools", "//tools/cargo-gnaw:install-cargo-gnaw", "//bundles:kitchen_sink"]`

From //root_build_dir/args.gn:3

**Overridden from the default:** `[]`

From //BUILD.gn:29

**Current value for `target_cpu = "x64"`:** `["//build/info:build-info", "//garnet/bin/log_listener:log_listener", "//garnet/bin/log_listener:log_listener_shell", "//garnet/bin/setui:setui_service", "//garnet/bin/sshd-host", "//garnet/bin/sshd-host:config", "//garnet/bin/sysmgr", "//garnet/bin/sysmgr:network_config", "//garnet/bin/sysmgr:services_config", "//garnet/bin/timezone", "//src/cobalt/bin/app:cobalt", "//src/cobalt/bin/app:cobalt_registry", "//src/cobalt/bin/app:config", "//src/cobalt/bin/system-metrics:cobalt_system_metrics", "//src/cobalt/bin/system-metrics:config", "//src/connectivity/bluetooth:core", "//src/connectivity/management/reachability", "//src/connectivity/management/reachability:reachability_sysmgr_config", "//src/connectivity/management:network_config_default", "//src/connectivity/network/mdns/bundles:config", "//src/connectivity/network/mdns/bundles:services", "//src/connectivity/network:config", "//src/connectivity/wlan:packages", "//src/connectivity/wlan/config:default", "//src/developer/forensics:pkg", "//src/developer/forensics/snapshot:pkg", "//src/diagnostics/archivist", "//src/diagnostics/archivist:with_default_config", "//src/hwinfo:hwinfo", "//src/hwinfo:default_product_config", "//src/media/audio/bundles:audio_config", "//src/recovery/factory_reset", "//src/security/policy:appmgr_policy_eng", "//src/security/root_ssl_certificates", "//src/sys/appmgr", "//src/sys/appmgr:appmgr_scheme_config", "//src/sys/appmgr:core_component_id_index", "//src/sys/core", "//src/sys/device_settings:device_settings_manager", "//src/sys/pkg:core", "//src/sys/pkg:pkgfs-disable-executability-restrictions", "//src/sys/pkg:system-update-checker", "//src/sys/pkg/bin/pkg-resolver:enable_dynamic_configuration", "//src/sys/stash:pkg", "//src/sys/time/network_time_service:network-time-service", "//src/sys/time/timekeeper", "//third_party/openssh-portable/fuchsia/developer-keys:ssh_config", "//src/sys/pkg:tools", "//tools/cargo-gnaw:install-cargo-gnaw", "//bundles:kitchen_sink"]`

From //root_build_dir/args.gn:3

**Overridden from the default:** `[]`

From //BUILD.gn:29

### basic_env_names
The list of environment names to include in "basic_envs".

**Current value (from the default):** `["emu"]`

From //build/testing/environments.gni:14

### blob_blobfs_maximum_bytes
For build/images:fvm.blob.sparse.blk, use this argument.

**Current value (from the default):** `""`

From //build/images/fvm.gni:75

### blob_blobfs_minimum_data_bytes
For build/images:fvm.blob.sparse.blk, use this argument.

**Current value (from the default):** `""`

From //build/images/fvm.gni:61

### blob_blobfs_minimum_inodes
For build/images:fvm.blob.sparse.blk, use this argument.

**Current value (from the default):** `""`

From //build/images/fvm.gni:50

### blobfs_maximum_bytes
In addition to reserving space for inodes and data, fs needs additional
space for maintaining some internal data structures. So the
space required to reserve inodes and data may exceed sum of the space
needed for inodes and data.
maximum_bytes puts an upper bound on the total bytes reserved for inodes,
data bytes and reservation for all other internal fs metadata.
An empty string does not put any upper bound. A filesystem may
reserve few blocks required for its operations.

**Current value (from the default):** `""`

From //build/images/fvm.gni:71

### blobfs_minimum_data_bytes
Number of bytes to reserve for data in the fs. This is in addition
to what is reserved, if any, for the inodes. Data bytes constitutes
"usable" space of the fs.
An empty string does not reserve any additional space than minimum
required for the filesystem.

**Current value (from the default):** `""`

From //build/images/fvm.gni:57

### blobfs_minimum_inodes
minimum_inodes is the number of inodes to reserve for the fs
An empty string does not reserve any additional space than minimum
required for the filesystem.

**Current value (from the default):** `""`

From //build/images/fvm.gni:46

### board_bootfs_labels
A list of binary labels to include in the ZBI.

**Current value for `target_cpu = "arm64"`:** `["//src/connectivity/ethernet/drivers/virtio:virtio_ethernet", "//src/devices/block/drivers/ahci", "//src/devices/block/drivers/virtio:virtio_block", "//src/devices/block/drivers/virtio:virtio_scsi", "//src/devices/misc/drivers/virtio-rng:virtio_rng", "//src/devices/misc/drivers/virtio-socket:virtio_socket", "//src/devices/serial/drivers/virtio-console:virtio_console", "//src/graphics/display/drivers/goldfish-display", "//src/graphics/drivers/virtio:virtio_gpu", "//src/ui/input/drivers/virtio:virtio_input", "//src/devices/board/drivers/qemu-arm64", "//src/devices/rtc/drivers/pl031-rtc", "//src/graphics/display/drivers/fake:fake-display", "//src/devices/bus/drivers/pci:bus-pci", "//src/devices/bus/drivers/pci:bus-pci.proxy", "//src/devices/usb/drivers/xhci-rewrite:xhci", "//src/security/policy/zxcrypt:null"]`

From //boards/arm64.gni:18

**Overridden from the default:** `[]`

From //build/board.gni:14

**Current value for `target_cpu = "x64"`:** `["//src/connectivity/ethernet/drivers/virtio:virtio_ethernet", "//src/devices/block/drivers/virtio:virtio_block", "//src/devices/block/drivers/virtio:virtio_scsi", "//src/devices/misc/drivers/virtio-rng:virtio_rng", "//src/devices/misc/drivers/virtio-socket:virtio_socket", "//src/devices/serial/drivers/virtio-console:virtio_console", "//src/graphics/display/drivers/goldfish-display", "//src/graphics/drivers/virtio:virtio_gpu", "//src/ui/input/drivers/virtio:virtio_input", "//src/media/audio/drivers/intel-hda/codecs/qemu:qemu-audio-codec", "//boards/kernel_cmdline:serial-legacy", "//src/connectivity/ethernet/drivers/realtek-8111", "//src/devices/bin/acpidump", "//src/devices/block/drivers/ahci", "//src/devices/block/drivers/mbr", "//src/devices/block/drivers/nvme", "//src/devices/block/drivers/pci-sdhci", "//src/devices/block/drivers/sdhci", "//src/devices/board/drivers/x86:platform-bus-x86", "//src/devices/bus/drivers/pci:bus-pci", "//src/devices/bus/drivers/pci:bus-pci.proxy", "//src/devices/i2c/drivers/intel-i2c", "//src/devices/pci/bin:bootfs", "//src/devices/rtc/drivers/intel-rtc", "//src/devices/serial/drivers/intel-serialio", "//src/devices/serial/drivers/uart16550", "//src/devices/usb/drivers/xhci:xhci-x86", "//src/graphics/display/drivers/intel-i915", "//src/graphics/display/drivers/simple:simple.amd-kaveri", "//src/graphics/display/drivers/simple:simple.bochs", "//src/graphics/display/drivers/simple:simple.intel", "//src/graphics/display/drivers/simple:simple.nv", "//src/graphics/display/drivers/simple:simple.vmware", "//src/media/audio/bin/ihda", "//src/media/audio/drivers/alc5514", "//src/media/audio/drivers/alc5663", "//src/media/audio/drivers/intel-hda/codecs/hdmi:hdmi-audio-codec", "//src/media/audio/drivers/intel-hda/codecs/realtek:realtek-audio-codec", "//src/media/audio/drivers/intel-hda/controller:intel-hda", "//src/media/audio/drivers/max98927", "//src/ui/input/drivers/i2c-hid", "//src/ui/input/drivers/pc-ps2", "//zircon/third_party/dev/ethernet/e1000", "//src/security/policy/zxcrypt:null"]`

From //boards/x64.gni:48

**Overridden from the default:** `[]`

From //build/board.gni:14

### board_extra_vbmeta_images
Board level extra vbmeta images to be combined into the top-level vbmeta
struct.

**Current value (from the default):** `[]`

From //build/images/vbmeta.gni:40

### board_has_libvulkan_arm_mali
Board files can set this to true if they have a package with a mali libvulkan VCD.

**Current value (from the default):** `false`

From //src/graphics/lib/magma/gnbuild/magma.gni:55

### board_name
Board name used for paving and amber updates.

**Current value for `target_cpu = "arm64"`:** `"qemu-arm64"`

From //boards/arm64.gni:9

**Overridden from the default:** `""`

From //build/board.gni:7

**Current value for `target_cpu = "x64"`:** `"x64"`

From //boards/x64.gni:9

**Overridden from the default:** `""`

From //build/board.gni:7

### board_package_labels
A list of package labels to include in the 'base' package set. Used by the
board definition rather than the product definition.

**Current value for `target_cpu = "arm64"`:** `["//garnet/bin/power_manager", "//garnet/bin/power_manager:base_config", "//garnet/bin/thermd", "//garnet/bin/thermd:config", "//garnet/packages/prod:drivers", "//src/media/audio/bundles:virtual_audio_driver"]`

From //boards/arm64.gni:24

**Overridden from the default:** `[]`

From //build/board.gni:11

**Current value for `target_cpu = "x64"`:** `["//garnet/bin/power_manager", "//garnet/bin/power_manager:base_config", "//garnet/bin/thermd", "//garnet/bin/thermd:config", "//garnet/packages/prod:drivers", "//src/hwinfo:default_board_config", "//src/media/audio/bundles:virtual_audio_driver"]`

From //boards/x64.gni:54

**Overridden from the default:** `[]`

From //build/board.gni:11

### board_recovery_bootfs_labels
A list of binary labels to include in the recovery ZBI.

**Current value for `target_cpu = "arm64"`:** `["//src/connectivity/ethernet/drivers/virtio:virtio_ethernet", "//src/devices/block/drivers/ahci", "//src/devices/block/drivers/virtio:virtio_block", "//src/devices/block/drivers/virtio:virtio_scsi", "//src/devices/misc/drivers/virtio-rng:virtio_rng", "//src/devices/misc/drivers/virtio-socket:virtio_socket", "//src/devices/serial/drivers/virtio-console:virtio_console", "//src/graphics/display/drivers/goldfish-display", "//src/graphics/drivers/virtio:virtio_gpu", "//src/ui/input/drivers/virtio:virtio_input", "//src/devices/board/drivers/qemu-arm64", "//src/devices/rtc/drivers/pl031-rtc", "//src/graphics/display/drivers/fake:fake-display", "//src/devices/bus/drivers/pci:bus-pci", "//src/devices/bus/drivers/pci:bus-pci.proxy", "//src/devices/usb/drivers/xhci-rewrite:xhci", "//src/security/policy/zxcrypt:null"]`

From //boards/arm64.gni:22

**Overridden from the default:** `[]`

From //build/board.gni:25

**Current value for `target_cpu = "x64"`:** `["//src/connectivity/ethernet/drivers/virtio:virtio_ethernet", "//src/devices/block/drivers/virtio:virtio_block", "//src/devices/block/drivers/virtio:virtio_scsi", "//src/devices/misc/drivers/virtio-rng:virtio_rng", "//src/devices/misc/drivers/virtio-socket:virtio_socket", "//src/devices/serial/drivers/virtio-console:virtio_console", "//src/graphics/display/drivers/goldfish-display", "//src/graphics/drivers/virtio:virtio_gpu", "//src/ui/input/drivers/virtio:virtio_input", "//src/media/audio/drivers/intel-hda/codecs/qemu:qemu-audio-codec", "//boards/kernel_cmdline:serial-legacy", "//src/connectivity/ethernet/drivers/realtek-8111", "//src/devices/bin/acpidump", "//src/devices/block/drivers/ahci", "//src/devices/block/drivers/mbr", "//src/devices/block/drivers/nvme", "//src/devices/block/drivers/pci-sdhci", "//src/devices/block/drivers/sdhci", "//src/devices/board/drivers/x86:platform-bus-x86", "//src/devices/bus/drivers/pci:bus-pci", "//src/devices/bus/drivers/pci:bus-pci.proxy", "//src/devices/i2c/drivers/intel-i2c", "//src/devices/pci/bin:bootfs", "//src/devices/rtc/drivers/intel-rtc", "//src/devices/serial/drivers/intel-serialio", "//src/devices/serial/drivers/uart16550", "//src/devices/usb/drivers/xhci:xhci-x86", "//src/graphics/display/drivers/intel-i915", "//src/graphics/display/drivers/simple:simple.amd-kaveri", "//src/graphics/display/drivers/simple:simple.bochs", "//src/graphics/display/drivers/simple:simple.intel", "//src/graphics/display/drivers/simple:simple.nv", "//src/graphics/display/drivers/simple:simple.vmware", "//src/media/audio/bin/ihda", "//src/media/audio/drivers/alc5514", "//src/media/audio/drivers/alc5663", "//src/media/audio/drivers/intel-hda/codecs/hdmi:hdmi-audio-codec", "//src/media/audio/drivers/intel-hda/codecs/realtek:realtek-audio-codec", "//src/media/audio/drivers/intel-hda/controller:intel-hda", "//src/media/audio/drivers/max98927", "//src/ui/input/drivers/i2c-hid", "//src/ui/input/drivers/pc-ps2", "//zircon/third_party/dev/ethernet/e1000", "//src/security/policy/zxcrypt:null"]`

From //boards/x64.gni:50

**Overridden from the default:** `[]`

From //build/board.gni:25

### board_tools
List of paths to board-specific tools to include in the build output.

Most development tools can just be used in-tree and do not need to be
included here. This arg is only meant for tools which may need to be
distributed along with the build files, for example tools for flashing
from SoC recovery mode.

Assets included in this way are included best-effort only and do not form
any kind of stable contract for users of the archive.

**Current value (from the default):** `[]`

From //build/images/args.gni:138

### board_zedboot_bootfs_labels
A list of binary labels to include in the zedboot ZBI.

**Current value for `target_cpu = "arm64"`:** `["//src/connectivity/ethernet/drivers/virtio:virtio_ethernet", "//src/devices/block/drivers/ahci", "//src/devices/block/drivers/virtio:virtio_block", "//src/devices/block/drivers/virtio:virtio_scsi", "//src/devices/misc/drivers/virtio-rng:virtio_rng", "//src/devices/misc/drivers/virtio-socket:virtio_socket", "//src/devices/serial/drivers/virtio-console:virtio_console", "//src/graphics/display/drivers/goldfish-display", "//src/graphics/drivers/virtio:virtio_gpu", "//src/ui/input/drivers/virtio:virtio_input", "//src/devices/board/drivers/qemu-arm64", "//src/devices/rtc/drivers/pl031-rtc", "//src/graphics/display/drivers/fake:fake-display", "//src/devices/bus/drivers/pci:bus-pci", "//src/devices/bus/drivers/pci:bus-pci.proxy", "//src/devices/usb/drivers/xhci-rewrite:xhci", "//src/security/policy/zxcrypt:null"]`

From //boards/arm64.gni:20

**Overridden from the default:** `[]`

From //build/board.gni:22

**Current value for `target_cpu = "x64"`:** `["//src/connectivity/ethernet/drivers/virtio:virtio_ethernet", "//src/devices/block/drivers/virtio:virtio_block", "//src/devices/block/drivers/virtio:virtio_scsi", "//src/devices/misc/drivers/virtio-rng:virtio_rng", "//src/devices/misc/drivers/virtio-socket:virtio_socket", "//src/devices/serial/drivers/virtio-console:virtio_console", "//src/graphics/display/drivers/goldfish-display", "//src/graphics/drivers/virtio:virtio_gpu", "//src/ui/input/drivers/virtio:virtio_input", "//src/media/audio/drivers/intel-hda/codecs/qemu:qemu-audio-codec", "//boards/kernel_cmdline:serial-legacy", "//src/connectivity/ethernet/drivers/realtek-8111", "//src/devices/bin/acpidump", "//src/devices/block/drivers/ahci", "//src/devices/block/drivers/mbr", "//src/devices/block/drivers/nvme", "//src/devices/block/drivers/pci-sdhci", "//src/devices/block/drivers/sdhci", "//src/devices/board/drivers/x86:platform-bus-x86", "//src/devices/bus/drivers/pci:bus-pci", "//src/devices/bus/drivers/pci:bus-pci.proxy", "//src/devices/i2c/drivers/intel-i2c", "//src/devices/pci/bin:bootfs", "//src/devices/rtc/drivers/intel-rtc", "//src/devices/serial/drivers/intel-serialio", "//src/devices/serial/drivers/uart16550", "//src/devices/usb/drivers/xhci:xhci-x86", "//src/graphics/display/drivers/intel-i915", "//src/graphics/display/drivers/simple:simple.amd-kaveri", "//src/graphics/display/drivers/simple:simple.bochs", "//src/graphics/display/drivers/simple:simple.intel", "//src/graphics/display/drivers/simple:simple.nv", "//src/graphics/display/drivers/simple:simple.vmware", "//src/media/audio/bin/ihda", "//src/media/audio/drivers/alc5514", "//src/media/audio/drivers/alc5663", "//src/media/audio/drivers/intel-hda/codecs/hdmi:hdmi-audio-codec", "//src/media/audio/drivers/intel-hda/codecs/realtek:realtek-audio-codec", "//src/media/audio/drivers/intel-hda/controller:intel-hda", "//src/media/audio/drivers/max98927", "//src/ui/input/drivers/i2c-hid", "//src/ui/input/drivers/pc-ps2", "//zircon/third_party/dev/ethernet/e1000", "//src/security/policy/zxcrypt:null"]`

From //boards/x64.gni:52

**Overridden from the default:** `[]`

From //build/board.gni:22

### board_zedboot_cmdline_args
List of kernel command line arguments to bake into the zedboot image that are
required by this board. See also zedboot_cmdline_args in
//build/images/zedboot/BUILD.gn

**Current value (from the default):** `[]`

From //build/board.gni:19

### bootfs_only
Put the "system image" package in the BOOTFS.  Hence what would
otherwise be /system/... at runtime is /boot/... instead.

**Current value for `target_cpu = "arm64"`:** `false`

From //products/core.gni:7

**Overridden from the default:** `false`

From //build/images/args.gni:14

**Current value for `target_cpu = "x64"`:** `false`

From //products/core.gni:7

**Overridden from the default:** `false`

From //build/images/args.gni:14

### build_all_vp9_file_decoder_conformance_tests

**Current value (from the default):** `false`

From //src/media/codec/examples/BUILD.gn:10

### build_id_format
Build ID algorithm to use for Fuchsia-target code.  This does not apply
to host or guest code.  The value is the argument to the linker's
`--build-id=...` switch.  If left empty (the default), the linker's
default format is used.

**Current value (from the default):** `""`

From //build/config/build_id.gni:10

### build_info_board
Board configuration of the current build

**Current value (from the default):** `"qemu-arm64"`

From //build/info/info.gni:12

### build_info_product
Product configuration of the current build

**Current value (from the default):** `""`

From //build/info/info.gni:9

### build_info_version
Logical version of the current build. If not set, defaults to the timestamp
of the most recent update.

**Current value (from the default):** `""`

From //build/info/info.gni:16

### build_libvulkan_arm_mali
Targets that will be built as mali vulkan ICDS.

**Current value (from the default):** `[]`

From //src/graphics/lib/magma/gnbuild/magma.gni:43

### build_libvulkan_goldfish

**Current value (from the default):** `""`

From //src/graphics/lib/goldfish-vulkan/gnbuild/BUILD.gn:12

### build_libvulkan_img_rgx
Targets that will be built as IMG vulkan ICDS.

**Current value (from the default):** `[]`

From //src/graphics/lib/magma/gnbuild/magma.gni:52

### build_libvulkan_qcom_adreno
Targets that will be built as qualcomm vulkan ICDS.

**Current value (from the default):** `[]`

From //src/graphics/lib/magma/gnbuild/magma.gni:49

### build_libvulkan_vsi_vip
Targets that will be built as verisilicon vulkan ICDS.

**Current value (from the default):** `[]`

From //src/graphics/lib/magma/gnbuild/magma.gni:46

### build_sdk_archives
Whether to build SDK tarballs.

**Current value (from the default):** `false`

From //build/sdk/config.gni:7

### cache_package_labels
If you add package labels to this variable, the packages will be included
in the 'cache' package set, which represents an additional set of software
that is made available on disk immediately after paving and in factory
flows. These packages are not updated with an OTA, but instead are updated
ephemerally. This cache of software can be evicted by the system if storage
pressure arises or other policies indicate.

**Current value for `target_cpu = "arm64"`:** `["//src/developer/ffx:runtime"]`

From //products/core.gni:83

**Overridden from the default:** `[]`

From //BUILD.gn:37

**Current value for `target_cpu = "x64"`:** `["//src/developer/ffx:runtime"]`

From //products/core.gni:83

**Overridden from the default:** `[]`

From //BUILD.gn:37

### camera_debug

**Current value (from the default):** `false`

From //src/camera/debug.gni:6

### camera_gym_configuration_cycle_time_ms

**Current value (from the default):** `10000`

From //src/camera/bin/camera-gym/BUILD.gn:11

### camera_gym_enable_root_presenter

**Current value (from the default):** `false`

From //src/camera/bin/camera-gym/BUILD.gn:12

### camera_policy_allow_replacement_connections
TODO(fxbug.dev/58063): Restore camera default exclusivity policy.

**Current value (from the default):** `true`

From //src/camera/bin/device/BUILD.gn:10

### check_production_eligibility
Whether to perform check on the build's eligibility for production.
If true, base_packages and cache_packages are checked against dependencies
on //build/validate:non_production_tag, which is used to tag any
non-production GN labels. Build will fail if such dependency is found.

**Current value (from the default):** `false`

From //build/images/args.gni:102

### clang_lib_dir
Path to Clang lib directory.

**Current value (from the default):** `"../build/prebuilt/third_party/clang/linux-x64/lib"`

From //build/images/manifest.gni:10

### clang_prefix
The default clang toolchain provided by the prebuilt. This variable is
additionally consumed by the Go toolchain.

**Current value (from the default):** `"../prebuilt/third_party/clang/linux-x64/bin"`

From //build/config/clang/clang.gni:12

### clang_tool_dir
Directory where the Clang toolchain binaries ("clang", "llvm-nm", etc.) are
found.  If this is "", then the behavior depends on $clang_prefix.
This toolchain is expected to support both Fuchsia targets and the host.

**Current value (from the default):** `""`

From //build/toolchain/zircon/clang.gni:11

### cobalt_environment
Selects the Cobalt environment to send data to. Choices:
  "LOCAL" - record log data locally to a file
  "DEVEL" - the non-prod environment for use in testing
  "PROD" - the production environment

**Current value (from the default):** `"PROD"`

From //src/cobalt/bin/app/BUILD.gn:15

### compress_blobs
Whether to compress the blobfs image.

**Current value (from the default):** `true`

From //build/images/args.gni:105

### concurrent_dart_jobs
Maximum number of Dart processes to run in parallel.

Dart analyzer uses a lot of memory which may cause issues when building
with many parallel jobs e.g. when using goma. To avoid out-of-memory
errors we explicitly reduce the number of jobs.

**Current value (from the default):** `32`

From //build/dart/BUILD.gn:15

### concurrent_go_jobs
Maximum number of Go processes to run in parallel.

**Current value (from the default):** `32`

From //build/go/BUILD.gn:11

### concurrent_link_jobs
Maximum number of concurrent link jobs.

We often want to run fewer links at once than we do compiles, because
linking is memory-intensive. The default to use varies by platform and by
the amount of memory available, so we call out to a script to get the right
value.

**Current value (from the default):** `32`

From //build/toolchain/BUILD.gn:15

### concurrent_rust_jobs
Maximum number of Rust processes to run in parallel.

We run multiple rustc jobs in parallel, each of which can cause significant
amount of memory, especially when using LTO. To avoid out-of-memory errors
we explicitly reduce the number of jobs.

**Current value (from the default):** `32`

From //build/rust/BUILD.gn:15

### config_have_heap
Tells openweave to include files that require heap access.

**Current value (from the default):** `true`

From [//third_party/openweave-core/config.gni:32](https://fuchsia.googlesource.com/third_party/openweave-core/+/5e90e806969a8f3dfc3050c08561db11761a3e6b/config.gni#32)

### crash_diagnostics_dir
Clang crash reports directory path. Use empty path to disable altogether.

**Current value (from the default):** `"//root_build_dir/clang-crashreports"`

From //build/config/clang/crash_diagnostics.gni:7

### crashpad_dependencies

**Current value (from the default):** `"fuchsia"`

From [//third_party/crashpad/build/crashpad_buildconfig.gni:22](https://chromium.googlesource.com/crashpad/crashpad/+/a98ee20e578f6febb828f5606c6ed1c02dc4e879/build/crashpad_buildconfig.gni#22)

### crashpad_use_boringssl_for_http_transport_socket

**Current value (from the default):** `true`

From [//third_party/crashpad/util/net/tls.gni:22](https://chromium.googlesource.com/crashpad/crashpad/+/a98ee20e578f6febb828f5606c6ed1c02dc4e879/util/net/tls.gni#22)

### current_cpu

**Current value (from the default):** `""`

### current_os

**Current value (from the default):** `""`

### custom_signing_script
If non-empty, the given script will be invoked to produce a signed ZBI
image. The given script must accept -z for the input zbi path, and -o for
the output signed zbi path. The path must be in GN-label syntax (i.e.
starts with //).

**Current value (from the default):** `""`

From //build/images/custom_signing.gni:12

### dart_compilation_mode

**Current value (from the default):** `"jit"`

From //build/dart/config.gni:16

### dart_default_app
Controls whether dart_app() targets generate JIT or AOT Dart snapshots.
This defaults to JIT, use `fx set <ARCH> --args
'dart_default_app="dart_aot_app"' to switch to AOT.

**Current value (from the default):** `"dart_jit_app"`

From [//topaz/runtime/dart/dart_component.gni:19](https://fuchsia.googlesource.com/topaz/+/c345ff778ea6e05a68bcdc4e414b0cab344e39bd/runtime/dart/dart_component.gni#19)

### dart_force_product
Forces all Dart and Flutter apps to build in a specific configuration that
we use to build products.

**Current value (from the default):** `false`

From [//topaz/runtime/dart/config.gni:10](https://fuchsia.googlesource.com/topaz/+/c345ff778ea6e05a68bcdc4e414b0cab344e39bd/runtime/dart/config.gni#10)

### dart_space_dart
Whether experimental space dart mode is enabled for Dart applications.

**Current value (from the default):** `false`

From [//topaz/runtime/dart/dart_component.gni:35](https://fuchsia.googlesource.com/topaz/+/c345ff778ea6e05a68bcdc4e414b0cab344e39bd/runtime/dart/dart_component.gni#35)

### data_partition_manifest
Path to manifest file containing data to place into the initial /data
partition.

**Current value (from the default):** `""`

From //build/images/args.gni:58

### debian_guest_earlycon

**Current value (from the default):** `false`

From //src/virtualization/packages/debian_guest/BUILD.gn:10

### debian_guest_qcow
Package the rootfs as a QCOW image (as opposed to a flat file).

**Current value (from the default):** `true`

From //src/virtualization/packages/debian_guest/BUILD.gn:9

### debuginfo
* `none` means no debugging information
* `backtrace` means sufficient debugging information to symbolize backtraces
* `debug` means debugging information suited for debugging

**Current value (from the default):** `"debug"`

From //build/config/compiler.gni:43

### devmgr_config
List of arguments to add to /boot/config/devmgr.
These come after synthesized arguments to configure blobfs and pkgfs.

**Current value (from the default):** `[]`

From //build/images/args.gni:18

### enable_api_diff
Detect dart API changes
TODO(fxb/36723, fxb/6623) Remove this flag once issues are resolved

**Current value (from the default):** `false`

From //build/dart/dart_library.gni:18

### enable_dart_analysis
Enable all dart analysis

**Current value (from the default):** `true`

From //build/dart/dart_library.gni:14

### enable_frame_pointers
Controls whether the compiler emits full stack frames for function calls.
This reduces performance but increases the ability to generate good
stack traces, especially when we have bugs around unwind table generation.
It applies only for Fuchsia targets (see below where it is unset).

TODO(ZX-2361): Theoretically unwind tables should be good enough so we can
remove this option when the issues are addressed.

**Current value (from the default):** `true`

From //build/config/BUILD.gn:24

### enable_gfx_subsystem

**Current value (from the default):** `true`

From //src/ui/scenic/bin/BUILD.gn:11

### enable_input_subsystem

**Current value (from the default):** `true`

From //src/ui/scenic/bin/BUILD.gn:12

### enable_mdns_trace
Enables the tracing feature of mdns, which can be turned on using
"mdns-util verbose".

**Current value (from the default):** `false`

From //src/connectivity/network/mdns/service/BUILD.gn:16

### enable_netboot
Whether to build the netboot zbi by default.

You can still build //build/images:netboot explicitly even if enable_netboot is false.

**Current value (from the default):** `false`

From //build/images/args.gni:63

### escher_test_for_glsl_spirv_mismatch
If true, this enables the |SpirvNotChangedTest| to check if the precompiled
shaders on disk are up to date and reflect the current shader source code
compiled with the latest shaderc tools/optimizations. People on the Scenic
team should build with this flag turned on to make sure that any shader
changes that were not run through the precompiler have their updated spirv
written to disk. Other teams and CQ do not need to worry about this flag.

**Current value (from the default):** `false`

From //src/ui/lib/escher/build_args.gni:18

### escher_use_runtime_glsl
Determines whether or not escher will build with the glslang and shaderc
libraries. When false, these libraries will not be included in the scenic/
escher binary and as a result shaders will not be able to be compiled at
runtime. Precompiled spirv code will be loaded into memory from disk instead.

**Current value (from the default):** `false`

From //src/ui/lib/escher/build_args.gni:10

### exclude_testonly_syscalls
If true, excludes syscalls with the [testonly] attribute.

**Current value (from the default):** `false`

From //zircon/vdso/vdso.gni:7

### expat_build_root

**Current value (from the default):** `"//third_party/expat"`

From //src/graphics/lib/magma/gnbuild/magma.gni:14

### experimental_wlan_client_mlme
Selects the SoftMAC client implementation to use. Choices:
  false (default) - C++ Client MLME implementation
  true - Rust Client MLME implementation
This argument is temporary until Rust MLME is ready to be used.

**Current value (from the default):** `false`

From //src/connectivity/wlan/lib/mlme/cpp/BUILD.gn:12

### extra_manifest_args
Extra args to globally apply to the manifest generation script.

**Current value (from the default):** `[]`

From //build/images/manifest.gni:16

### extra_package_labels

**Current value (from the default):** `[]`

From //third_party/cobalt/BUILD.gn:9

### extra_variants
Additional variant toolchain configs to support.
This is just added to [`known_variants`](#known_variants).

**Current value (from the default):** `[]`

From //build/config/BUILDCONFIG.gn:864

### fastboot_product

**Current value (from the default):** `""`

From //build/images/args.gni:85

### fidl_trace_level
0 = Disable FIDL userspace tracing (default).
1 = Enable FIDL userspace tracing.

**Current value (from the default):** `0`

From //build/fidl/args.gni:8

### firmware_prebuilts
List of prebuilt firmware blobs to include in update packages.

Each entry in the list is a scope containing:
 * `path`: path to the image (see also `firmware_prebuilts_path_suffix`)
 * `type`: firmware type, a device-specific unique identifier
 * `partition` (optional): if specified, the `fastboot flash` partition

**Current value (from the default):** `[]`

From //build/images/args.gni:43

### firmware_prebuilts_path_suffix
Suffix to append to all `firmware_prebuilts` `path` variables.

Typically this indicates the hardware revision, and is made available so
that users can easily switch revisions using a single arg.

**Current value (from the default):** `""`

From //build/images/args.gni:49

### flutter_default_app

**Current value (from the default):** `"flutter_jit_app"`

From [//topaz/runtime/dart/dart_component.gni:12](https://fuchsia.googlesource.com/topaz/+/c345ff778ea6e05a68bcdc4e414b0cab344e39bd/runtime/dart/dart_component.gni#12)

### flutter_driver_enabled
Enables/Disables flutter driver using '--args=flutter_driver_enabled=[true/false]'
in fx set. (Disabled by default)
This is effective only on debug builds.

**Current value (from the default):** `false`

From //build/testing/flutter_driver.gni:9

### flutter_profile

**Current value (from the default):** `true`

From [//topaz/runtime/dart/dart_component.gni:26](https://fuchsia.googlesource.com/topaz/+/c345ff778ea6e05a68bcdc4e414b0cab344e39bd/runtime/dart/dart_component.gni#26)

### flutter_space_dart
Whether experimental space dart mode is enabled for Flutter applications.

**Current value (from the default):** `false`

From [//topaz/runtime/dart/dart_component.gni:32](https://fuchsia.googlesource.com/topaz/+/c345ff778ea6e05a68bcdc4e414b0cab344e39bd/runtime/dart/dart_component.gni#32)

### font_catalog_paths

**Current value (from the default):** `["//prebuilt/third_party/fonts/fuchsia.font_catalog.json"]`

From //src/fonts/build/font_args.gni:17

### font_pkg_entries
Merged contents of .font_pkgs.json files. Populated in fonts.gni.

**Current value (from the default):** `[]`

From //src/fonts/build/font_args.gni:32

### font_pkgs_paths
Locations of .font_pkgs.json files, which list the locations of font files
within the workspace, as well as safe names that are derived from the fonts'
file names and can be used to name Fuchsia packages.

**Current value (from the default):** `["//prebuilt/third_party/fonts/fuchsia.font_pkgs.json"]`

From //src/fonts/build/font_args.gni:22

### fonts_dir
Directory into which all fonts are checked out from CIPD

**Current value (from the default):** `"//prebuilt/third_party/fonts"`

From //src/fonts/build/font_args.gni:12

### fuchsia_sdk_root
Consumers of the Fuchsia SDK instantiate templates for various SDK parts at
a specific spot within their buildroots. The target name for the specific
part is then derived from the part name as specified in the meta.json
manifest. Different buildroot instantiate the SDK parts at different
locations and then set this variable. GN rules can then prefix this variable
name in SDK builds to the name of the SDK part. This flag is meaningless in
non-SDK buildroots.

**Current value (from the default):** `""`

From //build/fuchsia/sdk.gni:17

### fvm_emmc_partition_size
The size in bytes of the FVM partition on the target eMMC devices.
Specifying this parameter will lead build to generate a fvm.fastboot.blk
suitable for flashing through fastboot for eMMC devices.

**Current value (from the default):** `""`

From //build/images/fvm.gni:17

### fvm_ftl_nand_block_count

**Current value (from the default):** `""`

From //build/images/fvm.gni:85

### fvm_ftl_nand_oob_size

**Current value (from the default):** `""`

From //build/images/fvm.gni:83

### fvm_ftl_nand_page_size
Specifying these variables will generate a NAND FVM image suitable for
directly flashing via fastboot. The NAND characteristics are required
in order to properly initialize the FTL metadata in the OOB area.
`max_fvm_size` should also be nonzero or else minfs will not have any
room to initialize on boot.

**Current value (from the default):** `""`

From //build/images/fvm.gni:82

### fvm_ftl_nand_pages_per_block

**Current value (from the default):** `""`

From //build/images/fvm.gni:84

### fvm_image_size
The size in bytes of the FVM partition image to create. Normally this is
computed to be just large enough to fit the blob and data images. The
default value is "", which means to size based on inputs. Specifying a size
that is too small will result in build failure.

**Current value (from the default):** `""`

From //build/images/fvm.gni:12

### fvm_max_disk_size
The max size of the disk where the FVM is written. This is used for
preallocating metadata to determine how much the FVM can expand on disk.
Only applies to sparse FVM images. At sparse image construction time, the
build fails if the inputs are larger than `fvm_max_disk_size`. At paving
time, the FVM will be sized to the target's disk size up to
`fvm_max_disk_size`. If the size of the disk increases after initial paving,
the FVM will resize up to `fvm_max_disk_size`. During paving, if the target
FVM has declared a smaller size than `fvm_max_disk_size`, the FVM is
reinitialized to the larger size.
The default value is "" which sets the max disk size to the size of the disk
at pave/format time.

**Current value (from the default):** `""`

From //build/images/fvm.gni:30

### fvm_partition

**Current value (from the default):** `""`

From //build/images/args.gni:83

### fvm_slice_size
The size of the FVM partition images "slice size". The FVM slice size is a
minimum size of a particular chunk of a partition that is stored within
FVM. A very small slice size may lead to decreased throughput. A very large
slice size may lead to wasted space. The selected default size of 8mb is
selected for conservation of space, rather than performance.

**Current value (from the default):** `"8388608"`

From //build/images/fvm.gni:37

### glm_build_root

**Current value (from the default):** `"//third_party/glm"`

From //src/graphics/lib/magma/gnbuild/magma.gni:17

### go_vet_enabled
  go_vet_enabled
    [bool] if false, go vet invocations are disabled for all builds.

**Current value (from the default):** `false`

From //build/go/go_build.gni:20

### gocache_dir
  gocache_dir
    Directory GOCACHE environment variable will be set to. This directory
    will have build and test results cached, and is safe to be written to
    concurrently. If overridden, this directory must be a full path.

**Current value (from the default):** `"/b/s/w/ir/k/root_build_dir/fidling/.gocache"`

From //build/go/go_build.gni:16

### goldfish_control_use_composite_device

**Current value (from the default):** `true`

From //src/graphics/drivers/misc/goldfish_control/BUILD.gn:9

### goma_dir
Directory containing the Goma source code.  This can be a GN
source-absolute path ("//...") or a system absolute path.

**Current value (from the default):** `"//prebuilt/third_party/goma/linux-x64"`

From //build/toolchain/goma.gni:15

### gpt_image
GUID Partition Table (GPT) image.

Typically useful for initially flashing a device from zero-state.

**Current value (from the default):** `""`

From //build/images/args.gni:54

### graphics_compute_generate_debug_shaders
Set to true in your args.gn file to generate pre-processed and
auto-formatted shaders under the "debug" sub-directory of HotSort
and Spinel target generation output directories.

These are never used, but can be reviewed manually to verify the
impact of configuration parameters, or when modifying a compute
shader.

Example results:

  out/default/
    gen/src/graphics/lib/compute/
       hotsort/targets/hs_amd_gcn3_u64/
          comp/
            hs_transpose.comp -> unpreprocessed shader
          debug/
            hs_transpose.glsl -> preprocessed shader


**Current value (from the default):** `true`

From //src/graphics/lib/compute/gn/glsl_shader_rules.gni:28

### graphics_compute_skip_spirv_opt
At times we may want to compare the performance of unoptimized
vs. optimized shaders.  On desktop platforms, use of spirv-opt
doesn't appear to provide major performance improvements but it
significantly reduces the size of the SPIR-V modules.

Disabling the spirv-opt pass may also be useful in identifying and
attributing code generation bugs.


**Current value (from the default):** `true`

From //src/graphics/lib/compute/gn/glsl_shader_rules.gni:38

### graphics_compute_verbose_compile
The glslangValidator compiler is noisy by default.  A cleanly
compiling shader still prints out its filename.

This negatively impacts the GN build.

For this reason, we silence the compiler with the "-s" option but
unfortunately this also disables all error reporting.

Set to true to see detailed error reporting.


**Current value (from the default):** `false`

From //src/graphics/lib/compute/gn/glsl_shader_rules.gni:50

### host_byteorder

**Current value (from the default):** `"undefined"`

From //build/config/host_byteorder.gni:7

### host_cpu

**Current value (from the default):** `"x64"`

### host_labels
If you add labels to this variable, these will be included in the 'host'
artifact set, which represents an additional set of host-only software that
is produced by the build.

**Current value for `target_cpu = "arm64"`:** `[]`

From //products/bringup.gni:34

**Overridden from the default:** `[]`

From //BUILD.gn:56

**Current value for `target_cpu = "x64"`:** `[]`

From //products/bringup.gni:34

**Overridden from the default:** `[]`

From //BUILD.gn:56

### host_os

**Current value (from the default):** `"linux"`

### host_tools_dir
This is the directory where host tools intended for manual use by
developers get installed.  It's something a developer might put
into their shell's $PATH.  Host tools that are just needed as part
of the build do not get copied here.  This directory is only for
things that are generally useful for testing or debugging or
whatnot outside of the GN build itself.  These are only installed
by an explicit install_host_tools() rule (see //build/host.gni).

**Current value (from the default):** `"//root_build_dir/host-tools"`

From //build/host.gni:13

### icu_disable_thin_archive
If true, compile icu into a standalone static library. Currently this is
only useful on Chrome OS.

**Current value (from the default):** `false`

From [//third_party/icu/config.gni:12](https://fuchsia.googlesource.com/third_party/icu/+/1be04b01dad80e4d77ddf711b2942288039f56b6/config.gni#12)

### icu_major_version_number
Contains the major version number of the ICU library, for dependencies that
need different configuration based on the library version. Currently this
is only useful in Fuchsia.

**Current value (from the default):** `"67"`

From [//third_party/icu/version.gni:9](https://fuchsia.googlesource.com/third_party/icu/+/1be04b01dad80e4d77ddf711b2942288039f56b6/version.gni#9)

### icu_use_data_file
Tells icu to load an external data file rather than rely on the icudata
being linked directly into the binary.

**Current value (from the default):** `true`

From [//third_party/icu/config.gni:8](https://fuchsia.googlesource.com/third_party/icu/+/1be04b01dad80e4d77ddf711b2942288039f56b6/config.gni#8)

### include_devmgr_config_in_vbmeta
If true, /config/devmgr config will be included into a vbmeta image
instead of bootfs.

**Current value (from the default):** `false`

From //build/images/vbmeta.gni:18

### include_fvm_blob_sparse
Include fvm.blob.sparse.blk image into the build if set to true

**Current value (from the default):** `false`

From //build/images/args.gni:115

### include_internal_fonts
Set to true to include internal fonts in the build.

**Current value (from the default):** `false`

From //src/fonts/build/font_args.gni:7

### include_tests_that_fail_on_nuc_asan
Whether to include tests that are known to fail on NUC with ASan.
Should be set to false in the infra builders that have board == "x64" and
"asan" in variants.

**Current value (from the default):** `true`

From //build/testing/environments.gni:11

### include_zxdb_large_tests
Normally these tests are not built and run because they require large amounts of optional data
be downloaded. Set this to true to enable the build for the zxdb_large_tests.
See symbols/test_data/README.md for how to download the data required for this test.

**Current value (from the default):** `false`

From //src/developer/debug/zxdb/BUILD.gn:13

### inet_config_enable_async_dns_sockets
Tells inet to support additionally support async dns sockets.

**Current value (from the default):** `true`

From [//third_party/openweave-core/config.gni:17](https://fuchsia.googlesource.com/third_party/openweave-core/+/5e90e806969a8f3dfc3050c08561db11761a3e6b/config.gni#17)

### inet_want_endpoint_dns
Tells inet to include support for the corresponding protocol.

**Current value (from the default):** `true`

From [//third_party/openweave-core/config.gni:10](https://fuchsia.googlesource.com/third_party/openweave-core/+/5e90e806969a8f3dfc3050c08561db11761a3e6b/config.gni#10)

### inet_want_endpoint_raw

**Current value (from the default):** `true`

From [//third_party/openweave-core/config.gni:11](https://fuchsia.googlesource.com/third_party/openweave-core/+/5e90e806969a8f3dfc3050c08561db11761a3e6b/config.gni#11)

### inet_want_endpoint_tcp

**Current value (from the default):** `true`

From [//third_party/openweave-core/config.gni:12](https://fuchsia.googlesource.com/third_party/openweave-core/+/5e90e806969a8f3dfc3050c08561db11761a3e6b/config.gni#12)

### inet_want_endpoint_tun

**Current value (from the default):** `true`

From [//third_party/openweave-core/config.gni:14](https://fuchsia.googlesource.com/third_party/openweave-core/+/5e90e806969a8f3dfc3050c08561db11761a3e6b/config.gni#14)

### inet_want_endpoint_udp

**Current value (from the default):** `true`

From [//third_party/openweave-core/config.gni:13](https://fuchsia.googlesource.com/third_party/openweave-core/+/5e90e806969a8f3dfc3050c08561db11761a3e6b/config.gni#13)

### is_analysis
If set, the build will produce compilation analysis dumps, used for code
cross-referencing in code search.  The extra work done during analysis
is only needed for cross-referencing builds, so we're keeping the flag
and the analysis overhead turned off by default.

**Current value (from the default):** `false`

From //build/config/BUILDCONFIG.gn:32

### is_debug
Debug build.

**Current value (from the default):** `true`

From //build/config/BUILDCONFIG.gn:35

### kernel_cmdline_args
List of kernel command line arguments to bake into the boot image.
See also [kernel_cmdline](/docs/reference/kernel/kernel_cmdline.md) and
[`devmgr_config`](#devmgr_config).

**Current value for `target_cpu = "arm64"`:** `["blobfs.userpager=true", "blobfs.cache-eviction-policy=NEVER_EVICT", "console.shell=true", "devmgr.log-to-debuglog=true", "kernel.enable-debugging-syscalls=true", "kernel.enable-serial-syscalls=true", "netsvc.all-features=true", "netsvc.disable=false", "kernel.oom.behavior=reboot"]`

From //products/core.gni:14

**Overridden from the default:** `[]`

From //build/images/args.gni:23

**Current value for `target_cpu = "x64"`:** `["blobfs.userpager=true", "blobfs.cache-eviction-policy=NEVER_EVICT", "console.shell=true", "devmgr.log-to-debuglog=true", "kernel.enable-debugging-syscalls=true", "kernel.enable-serial-syscalls=true", "netsvc.all-features=true", "netsvc.disable=false", "kernel.oom.behavior=reboot"]`

From //products/core.gni:14

**Overridden from the default:** `[]`

From //build/images/args.gni:23

### kernel_cmdline_files
Files containing additional kernel command line arguments to bake into
the boot image.  The contents of these files (in order) come after any
arguments directly in [`kernel_cmdline_args`](#kernel_cmdline_args).
These can be GN `//` source pathnames or absolute system pathnames.

**Current value (from the default):** `[]`

From //build/images/args.gni:29

### known_variants
List of variants that will form the basis for variant toolchains.
To make use of a variant, set [`select_variant`](#select_variant).

Normally this is not set as a build argument, but it serves to
document the available set of variants.
See also [`universal_variants`](#universal_variants).
Only set this to remove all the default variants here.
To add more, set [`extra_variants`](#extra_variants) instead.

Each element of the list is one variant, which is a scope defining:

  `configs` (optional)
      [list of labels] Each label names a config that will be
      automatically used by every target built in this variant.
      For each config `${label}`, there must also be a target
      `${label}_deps`, which each target built in this variant will
      automatically depend on.  The `variant()` template is the
      recommended way to define a config and its `_deps` target at
      the same time.

  `remove_common_configs` (optional)
  `remove_shared_configs` (optional)
      [list of labels] This list will be removed (with `-=`) from
      the `default_common_binary_configs` list (or the
      `default_shared_library_configs` list, respectively) after
      all other defaults (and this variant's configs) have been
      added.

  `deps` (optional)
      [list of labels] Added to the deps of every target linked in
      this variant (as well as the automatic `${label}_deps` for
      each label in configs).

  `name` (required if configs is omitted)
      [string] Name of the variant as used in
      [`select_variant`](#select_variant) elements' `variant` fields.
      It's a good idea to make it something concise and meaningful when
      seen as e.g. part of a directory name under `$root_build_dir`.
      If name is omitted, configs must be nonempty and the simple names
      (not the full label, just the part after all `/`s and `:`s) of these
      configs will be used in toolchain names (each prefixed by a "-"),
      so the list of config names forming each variant must be unique
      among the lists in `known_variants + extra_variants`.

  `tags` (optional)
      [list of strings] A list of liberal strings describing properties
      of the toolchain instances created from this variant scope. See
      //build/toolchain/variant_tags.gni for the list of available
      values and their meaning.

  `toolchain_args` (optional)
      [scope] Each variable defined in this scope overrides a
      build argument in the toolchain context of this variant.

  `host_only` (optional)
  `target_only` (optional)
      [scope] This scope can contain any of the fields above.
      These values are used only for host or target, respectively.
      Any fields included here should not also be in the outer scope.


**Current value (from the default):**
```
[{
  configs = ["//build/config/lto"]
}, {
  configs = ["//build/config/lto:thinlto"]
}, {
  configs = ["//build/config/profile"]
  tags = ["instrumented", "profile"]
}, {
  configs = ["//build/config/scudo"]
}, {
  configs = ["//build/config/sanitizers:ubsan"]
  tags = ["instrumented", "instrumentation-runtime"]
}, {
  configs = ["//build/config/sanitizers:ubsan", "//build/config/sanitizers:sancov"]
  tags = ["instrumented", "instrumentation-runtime"]
}, {
  configs = ["//build/config/sanitizers:asan"]
  host_only = {
  remove_shared_configs = ["//build/config:symbol_no_undefined"]
}
  tags = ["asan", "instrumentation-runtime", "instrumented", "lsan"]
  toolchain_args = {
  use_scudo = false
}
}, {
  configs = ["//build/config/sanitizers:asan", "//build/config/sanitizers:ubsan"]
  host_only = {
  remove_shared_configs = ["//build/config:symbol_no_undefined"]
}
  tags = ["asan", "instrumentation-runtime", "instrumented", "lsan"]
  toolchain_args = {
  use_scudo = false
}
}, {
  configs = ["//build/config/sanitizers:asan", "//build/config/sanitizers:sancov"]
  host_only = {
  remove_shared_configs = ["//build/config:symbol_no_undefined"]
}
  tags = ["asan", "instrumentation-runtime", "instrumented", "lsan"]
  toolchain_args = {
  use_scudo = false
}
}, {
  configs = ["//build/config/sanitizers:asan", "//build/config/fuzzer", "//build/config/sanitizers:rust-asan", "//build/config:icf"]
  host_only = {
  remove_shared_configs = ["//build/config:symbol_no_undefined"]
}
  name = "asan-fuzzer"
  remove_common_configs = ["//build/config:icf"]
  remove_shared_configs = ["//build/config:symbol_no_undefined"]
  tags = ["asan", "instrumentation-runtime", "instrumented", "lsan", "fuzzer"]
  toolchain_args = {
  asan_default_options = "alloc_dealloc_mismatch=0:check_malloc_usable_size=0:detect_odr_violation=0:max_uar_stack_size_log=16:print_scariness=1:allocator_may_return_null=1:detect_leaks=0:malloc_context_size=128:print_summary=1:print_suppressions=0:strict_memcmp=0:symbolize=0"
  use_scudo = false
}
}, {
  configs = ["//build/config/fuzzer", "//build/config/sanitizers:ubsan", "//build/config:icf"]
  name = "ubsan-fuzzer"
  remove_common_configs = ["//build/config:icf"]
  remove_shared_configs = ["//build/config:symbol_no_undefined"]
  tags = ["fuzzer", "instrumented", "instrumentation-runtime"]
}, {
  name = "gcc"
  tags = ["gcc"]
}]
```

From //build/config/BUILDCONFIG.gn:776

### launch_basemgr_on_boot
Indicates whether to include basemgr.cmx in the boot sequence for the
product image.

**Current value (from the default):** `true`

From //src/modular/build/modular_config/modular_config.gni:12

### linux_guest_extras_path

**Current value (from the default):** `""`

From //src/virtualization/packages/linux_guest/BUILD.gn:12

### linux_runner_extras_tests
If `true`, adds additional testonly content to extras.img, which will be
built and mounted inside the container at /mnt/chromeos.

**Current value (from the default):** `false`

From //src/virtualization/packages/biscotti_guest/linux_runner/BUILD.gn:25

### linux_runner_gateway

**Current value (from the default):** `"10.0.0.1"`

From //src/virtualization/packages/biscotti_guest/linux_runner/BUILD.gn:20

### linux_runner_ip
Default values for the guest network configuration.

These are currently hard-coded to match what is setup in the virtio-net
device.

See //src/virtualization/bin/vmm/device/virtio_net.cc for more details.

**Current value (from the default):** `"10.0.0.2"`

From //src/virtualization/packages/biscotti_guest/linux_runner/BUILD.gn:19

### linux_runner_netmask

**Current value (from the default):** `"255.255.255.0"`

From //src/virtualization/packages/biscotti_guest/linux_runner/BUILD.gn:21

### linux_runner_volatile_block
If `true`, all block devices that would normally load as READ_WRITE will
be loaded as VOLATILE_WRITE. This is useful when working on changes to
the linux kernel as crashes and panics can sometimes corrupt the images.

**Current value (from the default):** `false`

From //src/virtualization/packages/biscotti_guest/linux_runner/BUILD.gn:30

### local_bench
Used to enable local benchmarking/fine-tuning when running benchmarks
in `fx shell`. Pass `--args=local_bench='true'` to `fx set` in order to
enable it.

**Current value (from the default):** `false`

From //src/developer/fuchsia-criterion/BUILD.gn:14

### log_startup_sleep

**Current value (from the default):** `"30000"`

From //garnet/bin/log_listener/BUILD.gn:15

### lsan_default_options
Default [LeakSanitizer](https://clang.llvm.org/docs/LeakSanitizer.html)
options (before the `LSAN_OPTIONS` environment variable is read at
runtime).  This can be set as a build argument to affect most "lsan"
variants in $variants (which see), or overridden in $toolchain_args in
one of those variants.  This can be a list of strings or a single string.

Note that even if this is empty, programs in this build **cannot** define
their own `__lsan_default_options` C function.  Instead, they can use a
sanitizer_extra_options() target in their `deps` and then any options
injected that way can override that option's setting in this list.

**Current value (from the default):** `[]`

From //zircon/public/gn/config/instrumentation/sanitizer_default_options.gni:28

### magma_build_root

**Current value (from the default):** `"//src/graphics/lib/magma"`

From //src/graphics/lib/magma/gnbuild/magma.gni:13

### magma_enable_developer_build
Enable this to have the msd include a suite of tests and invoke them
automatically when the driver starts.

**Current value (from the default):** `false`

From //src/graphics/lib/magma/gnbuild/magma.gni:27

### magma_enable_tracing
Enable this to include fuchsia tracing capability

**Current value (from the default):** `true`

From //src/graphics/lib/magma/gnbuild/magma.gni:23

### magma_openvx_include
The path to OpenVX headers

**Current value (from the default):** `""`

From //src/graphics/lib/magma/gnbuild/magma.gni:35

### magma_openvx_package
The path to an OpenVX implementation

**Current value (from the default):** `""`

From //src/graphics/lib/magma/gnbuild/magma.gni:38

### magma_python_path

**Current value (from the default):** `"/b/s/w/ir/k/third_party/mako"`

From //src/graphics/lib/magma/gnbuild/magma.gni:20

### max_blob_contents_size
Maximum allowable contents for the /blob in a release mode build.
Zero means no limit.
contents_size refers to contents stored within the filesystem (regardless
of how they are stored).

**Current value (from the default):** `"0"`

From //build/images/filesystem_limits.gni:10

### max_blob_image_size
Maximum allowable image_size for /blob in a release mode build.
Zero means no limit.
image_size refers to the total image size, including both contents and
metadata.

**Current value (from the default):** `"0"`

From //build/images/filesystem_limits.gni:16

### max_data_contents_size
Maximum allowable contents_size for /data in a release mode build.
Zero means no limit.
contents_size refers to contents stored within the filesystem (regardless
of how they are stored).

**Current value (from the default):** `"0"`

From //build/images/filesystem_limits.gni:22

### max_data_image_size
Maximum allowable image_size for /data in a release mode build.
Zero means no limit.
image_size refers to the total image size, including both contents and
metadata.

**Current value (from the default):** `"0"`

From //build/images/filesystem_limits.gni:28

### max_fuchsia_zbi_size
Maximum allowable size for fuchsia.zbi

**Current value for `target_cpu = "arm64"`:** `"16777216"`

From //boards/arm64.gni:35

**Overridden from the default:** `"0"`

From //build/images/filesystem_limits.gni:31

**Current value for `target_cpu = "x64"`:** `"16777216"`

From //boards/x64.gni:66

**Overridden from the default:** `"0"`

From //build/images/filesystem_limits.gni:31

### max_fvm_size
Maximum allowable size for the FVM in a release mode build
Zero means no limit

**Current value (from the default):** `"0"`

From //build/images/max_fvm_size.gni:8

### max_log_disk_usage
Controls how many bytes of space on disk are used to persist device logs.
Should be a string value that only contains digits.

**Current value (from the default):** `"0"`

From //garnet/bin/log_listener/BUILD.gn:14

### max_zedboot_zbi_size
Maximum allowable size for zedboot.zbi

**Current value for `target_cpu = "arm64"`:** `"16777216"`

From //boards/arm64.gni:36

**Overridden from the default:** `"0"`

From //build/images/filesystem_limits.gni:34

**Current value for `target_cpu = "x64"`:** `"16777216"`

From //boards/x64.gni:67

**Overridden from the default:** `"0"`

From //build/images/filesystem_limits.gni:34

### meta_package_labels
A list of labels for meta packages to be included in the monolith.

**Current value for `target_cpu = "arm64"`:** `["//build/images:config-data", "//build/images:shell-commands", "//src/sys/component_index:component_index"]`

From //products/core.gni:16

**Overridden from the default:** `[]`

From //build/images/args.gni:66

**Current value for `target_cpu = "x64"`:** `["//build/images:config-data", "//build/images:shell-commands", "//src/sys/component_index:component_index"]`

From //products/core.gni:16

**Overridden from the default:** `[]`

From //build/images/args.gni:66

### minfs_maximum_bytes

**Current value (from the default):** `""`

From //build/images/fvm.gni:72

### minfs_minimum_data_bytes

**Current value (from the default):** `""`

From //build/images/fvm.gni:58

### minfs_minimum_inodes

**Current value (from the default):** `""`

From //build/images/fvm.gni:47

### msd_arm_enable_all_cores
Enable all 8 cores, which is faster but emits more heat.

**Current value (from the default):** `true`

From //src/graphics/drivers/msd-arm-mali/src/BUILD.gn:9

### msd_arm_enable_cache_coherency
With this flag set the system tries to use cache coherent memory if the
GPU supports it.

**Current value (from the default):** `true`

From //src/graphics/drivers/msd-arm-mali/src/BUILD.gn:13

### msd_arm_enable_protected_debug_swap_mode
In protected mode, faults don't return as much information so they're much harder to debug. To
work around that, add a mode where protected atoms are executed in non-protected mode and
vice-versa.

NOTE: The memory security ranges should also be set (in TrustZone) to the opposite of normal, so
that non-protected mode accesses can only access protected memory and vice versa.  Also,
growable memory faults won't work in this mode, so larger portions of growable memory should
precommitted (which is not done by default).

**Current value (from the default):** `false`

From //src/graphics/drivers/msd-arm-mali/src/BUILD.gn:23

### msd_build_root

**Current value (from the default):** `"//src/graphics/drivers"`

From //src/graphics/lib/magma/gnbuild/magma.gni:15

### msd_intel_gen_build_root

**Current value (from the default):** `"//src/graphics/drivers/msd-intel-gen"`

From //src/graphics/lib/magma/gnbuild/magma.gni:16

### netcfg_autostart

**Current value (from the default):** `true`

From //src/connectivity/management/BUILD.gn:6

### netsvc_extra_defines

**Current value (from the default):** `[]`

From //src/bringup/bin/netsvc/BUILD.gn:11

### omaha_app_id
Default app id will always return no update.

**Current value (from the default):** `"fuchsia-test:no-update"`

From //src/sys/pkg/bin/omaha-client/BUILD.gn:15

### optimize
* `none`: really unoptimized, usually only build-tested and not run
* `debug`: "optimized for debugging", light enough to avoid confusion
* `default`: default optimization level
* `size`:  optimized for space rather than purely for speed
* `speed`: optimized purely for speed
* `sanitizer`: optimized for sanitizers (ASan, etc.)
* `profile`: optimized for coverage/profile data collection

**Current value (from the default):** `"debug"`

From //build/config/compiler.gni:22

### output_breakpad_syms
Sets if we should output breakpad symbols for Fuchsia binaries.

**Current value (from the default):** `false`

From //build/config/BUILDCONFIG.gn:38

### output_gsym
Controls whether we should output GSYM files for Fuchsia binaries.

**Current value (from the default):** `false`

From //build/config/BUILDCONFIG.gn:41

### override_recovery_label
TODO(comfoltey) remove obsolete label override_recovery_label

**Current value (from the default):** `""`

From //build/images/args.gni:127

### persist_logs

**Current value (from the default):** `false`

From //build/persist_logs.gni:13

### pkgfs_packages_allowlist

**Current value (from the default):** `"//src/security/policy/pkgfs_non_static_pkgs_allowlist_eng.txt"`

From //build/images/args.gni:112

### platform_enable_user_pci

**Current value (from the default):** `false`

From //src/devices/bus/drivers/pci/pci.gni:10

### pre_erase_flash

**Current value (from the default):** `false`

From //build/images/args.gni:86

### prebuilt_dart_sdk
Directory containing prebuilt Dart SDK.
This must have in its `bin/` subdirectory `gen_snapshot.OS-CPU` binaries.

**Current value (from the default):** `"//prebuilt/third_party/dart/linux-x64"`

From //build/dart/dart.gni:8

### prebuilt_libvulkan_arm_path

**Current value (from the default):** `""`

From //src/graphics/lib/magma/gnbuild/magma.gni:29

### prebuilt_libvulkan_img_path
The path to a prebuilt libvulkan.so for an IMG GPU.

**Current value (from the default):** `""`

From //src/graphics/lib/magma/gnbuild/magma.gni:32

### product_bootfs_labels
A list of binary labels to include in ZBIs built for this product.

**Current value for `target_cpu = "arm64"`:** `["//src/sys/component_manager:component_manager_config_bootfs_resource"]`

From //products/bringup.gni:20

**Overridden from the default:** `[]`

From //build/product.gni:7

**Current value for `target_cpu = "x64"`:** `["//src/sys/component_manager:component_manager_config_bootfs_resource"]`

From //products/bringup.gni:20

**Overridden from the default:** `[]`

From //build/product.gni:7

### product_include_updates_in_blobfs
Include update package in blob.blk. Some products may not need the update
package included as part of the blobfs.
TODO(58645) Remove when no longer needed.

**Current value (from the default):** `true`

From //build/product.gni:16

### product_zedboot_bootfs_labels
A list of binary labels to include in the zedboot ZBI built for this
product.

**Current value for `target_cpu = "arm64"`:** `["//src/sys/component_manager:component_manager_config_bootfs_resource"]`

From //products/bringup.gni:22

**Overridden from the default:** `[]`

From //build/product.gni:11

**Current value for `target_cpu = "x64"`:** `["//src/sys/component_manager:component_manager_config_bootfs_resource"]`

From //products/bringup.gni:22

**Overridden from the default:** `[]`

From //build/product.gni:11

### prototype_account_transfer
Whether or not prototype account transfer is enabled.
NOTE: This is not secure and should NOT be enabled for any products!  This
is only for use during local development.

**Current value (from the default):** `false`

From //src/identity/bin/account_manager/BUILD.gn:12

### recovery_label
Allows a product to specify the recovery image used in the zirconr slot.
Default recovery image is zedboot. Overriding this value will keep zedboot
in the build but will not include it as the default zirconr image.
Recovery images can provide an update target by specifying the metadata item
"update_target" in the format <target>=<path>. (Such as `update_target =
[ "recovery=" + rebase_path(recovery_path, root_build_dir) ]`)
Example value: "//build/images/recovery"

**Current value (from the default):** `"//build/images/zedboot"`

From //build/images/args.gni:124

### recovery_logo_path
Path to file to use for recovery logo

**Current value (from the default):** `"//src/recovery/system/res/fuchsia-logo.png"`

From //src/recovery/system/system_recovery_args.gni:7

### rust_cap_lints
Sets the maximum lint level.
"deny" will make all warnings into errors, "warn" preserves them as warnings, and "allow" will
ignore warnings.

**Current value (from the default):** `"deny"`

From //build/rust/config.gni:54

### rust_lto
Sets the default LTO type for rustc bulids.

**Current value (from the default):** `""`

From //build/rust/config.gni:46

### rust_override_lto
Overrides the LTO setting for all Rust builds, regardless of
debug/release flags or the `with_lto` arg to the rustc_ templates.
Valid values are "none", "thin", and "fat".

**Current value (from the default):** `""`

From //build/rust/config.gni:64

### rust_override_opt
Overrides the optimization level for all Rust builds, regardless of
debug/release flags or the `force_opt` arg to the rustc_ templates.
Valid values are 0-3, o, and z.

**Current value (from the default):** `""`

From //build/rust/config.gni:59

### rust_sysroot
Sets a custom base directory for where rust tooling
looks for the standard library

**Current value (from the default):** `"../prebuilt/third_party/rust/linux-x64"`

From //build/rust/config.gni:43

### rust_toolchain_triple_suffix
Sets the fuchsia toolchain target triple suffix (after arch)

**Current value (from the default):** `"fuchsia"`

From //build/rust/config.gni:49

### rust_v0_symbol_mangling
Controls whether the rust compiler uses v0 symbol mangling scheme
(see https://github.com/rust-lang/rfcs/blob/master/text/2603-rust-symbol-name-mangling-v0.md).
The v0 symbol mangling scheme requires upstream LLVM support when demangling,
so it is not on by default.
TODO(fxbug.dev/57302): Enable v0 mangling by default.

**Current value (from the default):** `false`

From //build/config/BUILD.gn:32

### rustc_lib_dir
Path to rustc lib directory.

**Current value (from the default):** `"../build/prebuilt/third_party/rust/linux-x64/lib"`

From //build/images/manifest.gni:13

### rustc_prefix
Sets a custom base directory for `rustc` and `cargo`.
This can be used to test custom Rust toolchains.

**Current value (from the default):** `"../prebuilt/third_party/rust/linux-x64/bin"`

From //build/rust/config.gni:25

### rustc_version_description
Human-readable identifier for the toolchain version.

TODO(tmandry): Make this the same repo/revision info from `rustc --version`.
e.g., clang_version_description = read_file("$_rustc_lib_dir/VERSION")

**Current value (from the default):** `""`

From //build/rust/config.gni:39

### rustc_version_string
This is a string identifying the particular toolchain version in use.  Its
only purpose is to be unique enough that it changes when switching to a new
toolchain, so that recompilations with the new compiler can be triggered.

When using the prebuilt, this is ignored and the CIPD instance ID of the
prebuilt is used.

**Current value (from the default):** `"0v1jaeyeb9K3EGyl_O56bQ02Nt1CCd3_JeANRsXANBUC"`

From //build/rust/config.gni:33

### scenic_display_frame_number
Draws the current frame number in the top-left corner.

**Current value (from the default):** `false`

From //src/ui/scenic/lib/gfx/BUILD.gn:11

### scenic_enable_vulkan_validation
Include the vulkan validation layers in scenic.

**Current value (from the default):** `true`

From //src/ui/scenic/BUILD.gn:115

### scenic_ignore_vsync

**Current value (from the default):** `false`

From //src/ui/scenic/lib/gfx/BUILD.gn:8

### scudo_default_options
Default [Scudo](https://llvm.org/docs/ScudoHardenedAllocator.html)
options (before the `SCUDO_OPTIONS` environment variable is read at
runtime).  *NOTE:* This affects only components using the `scudo`
variant (see GN build argument `select_variant`), and does not affect
anything when the `use_scudo` build flag is set instead.

**Current value (from the default):** `["abort_on_error=1", "QuarantineSizeKb=0", "ThreadLocalQuarantineSizeKb=0", "DeallocationTypeMismatch=false", "DeleteSizeMismatch=false", "allocator_may_return_null=true"]`

From //build/config/scudo/scudo.gni:17

### sdk_dirs
The directories to search for parts of the SDK.

By default, we search the public directories for the various layers.
In the future, we'll search a pre-built SDK as well.

**Current value (from the default):** `["//garnet/public", "//topaz/public"]`

From //build/config/fuchsia/sdk.gni:10

### sdk_id
Identifier for the Core SDK.

**Current value (from the default):** `""`

From //sdk/config.gni:7

### select_variant
List of "selectors" to request variant builds of certain targets.
Each selector specifies matching criteria and a chosen variant.
The first selector in the list to match a given target determines
which variant is used for that target.

Each selector is either a string or a scope.  A shortcut selector is
a string; it gets expanded to a full selector.  A full selector is a
scope, described below.

A string selector can match a name in
[`select_variant_shortcuts`](#select_variant_shortcuts).  If it's not a
specific shortcut listed there, then it can be the name of any variant
described in [`known_variants`](#known_variants) and
[`universal_variants`](#universal_variants) (and combinations thereof).
A `selector` that's a simple variant name selects for every binary
built in the target toolchain: `{ host=false variant=selector }`.

If a string selector contains a slash, then it's `"shortcut/filename"`
and selects only the binary in the target toolchain whose `output_name`
matches `"filename"`, i.e. it adds `output_name=["filename"]` to each
selector scope that the shortcut's name alone would yield.

The scope that forms a full selector defines some of these:

    variant (required)
        [string or `false`] The variant that applies if this selector
        matches.  This can be `false` to choose no variant, or a string
        that names the variant.  See
        [`known_variants`](#known_variants) and
        [`universal_variants`](#universal_variants).

The rest below are matching criteria.  All are optional.
The selector matches if and only if all of its criteria match.
If none of these is defined, then the selector always matches.

The first selector in the list to match wins and then the rest of
the list is ignored.  To construct more complex rules, use a blocklist
selector with `variant=false` before a catch-all default variant, or
a list of specific variants before a catch-all false variant.

Each "[strings]" criterion is a list of strings, and the criterion
is satisfied if any of the strings matches against the candidate string.

    host
        [boolean] If true, the selector matches in the host toolchain.
        If false, the selector matches in the target toolchain.

    testonly
        [boolean] If true, the selector matches targets with testonly=true.
        If false, the selector matches in targets without testonly=true.

    target_type
        [strings]: `"executable"`, `"loadable_module"`, or `"driver_module"`

    output_name
        [strings]: target's `output_name` (default: its `target name`)

    label
        [strings]: target's full label with `:` (without toolchain suffix)

    name
        [strings]: target's simple name (label after last `/` or `:`)

    dir
        [strings]: target's label directory (`//dir` for `//dir:name`).

**Current value (from the default):** `[]`

From //build/config/BUILDCONFIG.gn:1091

### select_variant_canonical
*This should never be set as a build argument.*
It exists only to be set in `toolchain_args`.
See //build/toolchain/clang_toolchain.gni for details.

**Current value (from the default):** `[]`

From //build/config/BUILDCONFIG.gn:1096

### select_variant_shortcuts
List of short names for commonly-used variant selectors.  Normally this
is not set as a build argument, but it serves to document the available
set of short-cut names for variant selectors.  Each element of this list
is a scope where `.name` is the short name and `.select_variant` is a
a list that can be spliced into [`select_variant`](#select_variant).

**Current value (from the default):**
```
[{
  name = "host_asan"
  select_variant = [{
  host = true
  variant = "asan"
}]
}, {
  name = "host_asan-ubsan"
  select_variant = [{
  host = true
  variant = "asan-ubsan"
}]
}, {
  name = "host_profile"
  select_variant = [{
  host = true
  variant = "profile"
}]
}, {
  name = "kasan"
  select_variant = []
}, {
  name = "kasan-sancov"
  select_variant = []
}]
```

From //build/config/BUILDCONFIG.gn:910

### shaderc_enable_spvc_parser
Enables using the parsing built into spvc instead spirv-cross

**Current value (from the default):** `false`

From [//third_party/shaderc/shaderc_features.gni:17](https://fuchsia.googlesource.com/third_party/shaderc/+/ae50f26a6453fd8f8cd148fbd62a6ae9a94d4472/shaderc_features.gni#17)

### shaderc_spvc_disable_context_logging
Disables logging to messages in context struct

**Current value (from the default):** `false`

From [//third_party/shaderc/shaderc_features.gni:23](https://fuchsia.googlesource.com/third_party/shaderc/+/ae50f26a6453fd8f8cd148fbd62a6ae9a94d4472/shaderc_features.gni#23)

### shaderc_spvc_enable_direct_logging
Enables logging directly out to the terminal

**Current value (from the default):** `false`

From [//third_party/shaderc/shaderc_features.gni:20](https://fuchsia.googlesource.com/third_party/shaderc/+/ae50f26a6453fd8f8cd148fbd62a6ae9a94d4472/shaderc_features.gni#20)

### size_checker_input
The input to the size checker.
The build system will produce a JSON file to be consumed by the size checker, which
will check and prevent integration of subsystems that are over their space allocation.
The input consists of the following keys:

asset_ext(string array): a list of extensions that should be considered as assets.

asset_limit(number): maximum size (in bytes) allocated for the assets.

core_limit(number): maximum size (in bytes) allocated for the core system and/or services.
This is sort of a "catch all" component that consists of all the area / packages that weren't
specified in the components list below.

components(object array): a list of component objects. Each object should contain the following keys:

  component(string): name of the component.

  src(string array): path of the area / package to be included as part of the component.
  The path should be relative to the obj/ in the output directory.
  For example, consider two packages foo and far, built to out/.../obj/some_big_component/foo and out/.../obj/some_big_component/bar.
  If you want to impose a limit on foo, your src will be ["some_big_component/foo"].
  If you want to impose a limit on both foo and far, your src will be ["some_big_component"].
  If a package has config-data, those prebuilt blobs actually live under the config-data package.
  If you wish to impose a limit of those data as well, you should add "build/images/config-data/$for_pkg" to your src.
  The $for_pkg corresponds to the $for_pkg field in config.gni.

  limit(number): maximum size (in bytes) allocated for the component.

Example:
size_checker_input = {
  asset_ext = [ ".ttf" ]
  asset_limit = 10240
  core_limit = 10240
  components = [
    {
      component = "Foo"
      src = [ "topaz/runtime/foo_runner" ]
      limit = 10240
    },
    {
      component = "Bar"
      src = [ "build/images" ]
      limit = 20480
    },
  ]
}

**Current value (from the default):** `{ }`

From //tools/size_checker/cmd/BUILD.gn:52

### sysmgr_golden_warn_override
Used by config_package().
If true, then overrides the value of the sysmgr_golden_warn template
parameter to true regardless of whether it is specified on a target.

**Current value (from the default):** `false`

From //build/config.gni:12

### syzkaller_dir
Used by syz-ci to build with own syz-executor source.

**Current value (from the default):** `"//third_party/syzkaller"`

From //src/testing/fuzzing/syzkaller/BUILD.gn:11

### target_cpu

**Current value for `target_cpu = "arm64"`:** `"arm64"`

From //boards/arm64.gni:7

**Overridden from the default:** `""`

**Current value for `target_cpu = "x64"`:** `"x64"`

From //boards/x64.gni:7

**Overridden from the default:** `""`

### target_os

**Current value (from the default):** `""`

### target_sysroot
The absolute path of the sysroot that is used with the target toolchain.

**Current value (from the default):** `""`

From //build/config/sysroot.gni:7

### termina_disk
The termina disk image.

Defaults to the disk image from CIPD, but can be overridden to use a
custom disk for development purposes.

**Current value (from the default):** `"//prebuilt/virtualization/packages/termina_guest/images/arm64/vm_rootfs.img"`

From //src/virtualization/packages/termina_guest/BUILD.gn:18

### termina_kernel
The termina kernel image.

Defaults to the common linux kernel image from CIPD, but can be overridden to use a
custom kernel for development purposes.

**Current value (from the default):** `"//prebuilt/virtualization/packages/linux_guest/images/arm64/Image"`

From //src/virtualization/packages/termina_guest/BUILD.gn:12

### test_durations_file
A file in integration containing historical test duration data for this
build configuration. This file is used by infra to efficiently schedule
tests. "default.json" is a dummy file that contains no real duration data,
and causes infra to schedule tests as if each one has the same duration.

**Current value (from the default):** `"//integration/infra/test_durations/default.json"`

From //BUILD.gn:43

### thinlto_cache_dir
ThinLTO cache directory path.

**Current value (from the default):** `"dartlang/thinlto-cache"`

From //build/config/lto/config.gni:16

### thinlto_jobs
Number of parallel ThinLTO jobs.

**Current value (from the default):** `8`

From //build/config/lto/config.gni:13

### time_trace
Whether to export time traces when building with clang.
https://releases.llvm.org/9.0.0/tools/clang/docs/ReleaseNotes.html#new-compiler-flags

**Current value (from the default):** `false`

From //build/config/clang/time_trace.gni:8

### toolchain_variant
*This should never be set as a build argument.*
It exists only to be set in `toolchain_args`.
See //build/toolchain/clang_toolchain.gni for details.
This variable is a scope giving details about the current toolchain:
    `toolchain_variant.base`
        [label] The "base" toolchain for this variant, *often the
        right thing to use in comparisons, not `current_toolchain`.*
        This is the toolchain actually referenced directly in GN
        source code.  If the current toolchain is not
        `shlib_toolchain` or a variant toolchain, this is the same
        as `current_toolchain`.  In one of those derivative
        toolchains, this is the toolchain the GN code probably
        thought it was in.  This is the right thing to use in a test
        like `toolchain_variant.base == target_toolchain`, rather
        rather than comparing against `current_toolchain`.
    `toolchain_variant.name`
        [string] The name of this variant, as used in `variant` fields
        in [`select_variant`](#select_variant) clauses.  In the base
        toolchain and its `shlib_toolchain`, this is `""`.
    `toolchain_variant.suffix`
        [string] This is "-${toolchain_variant.name}", "" if name is empty.
    `toolchain_variant.is_pic_default`
        [bool] This is true in `shlib_toolchain`.
    `toolchain_variant.tags`
        [list of strings] A list of liberal strings, each one describing a
        property of this toolchain instance. See
        //build/toolchain/variant_tags.gni for more details.
    `toolchain_variant.configs`
        [list of configs] A list of configs that are added to all linkable
        targets for this toolchain() instance.
    `toolchain_variant.remove_common_configs`
        [list of configs] A list of configs that are removed from all
        linkable targets for this toolchain() instance. Useful when
        one of the default configs must not be used.
    `toolchain_variant.remove_shared_configs`
        [list of configs] Same a remove_common_configs, but only applies
        to non-executable (e.g. shared_library()) targets.
    `toolchain_variant.instrumented`
        [boolean] A convenience flag that is true iff "instrumented" is
        part of toolchain_variant.tags.
    `toolchain_variant.with_shared`
        [boolean] True iff this toolchain() instance has a secondary
        toolchain to build ELF shared-library code.

The other fields are the variant's effects as defined in
[`known_variants`](#known_variants).

**Current value (from the default):**
```
{
  base = "//build/toolchain/fuchsia:arm64"
}
```

From //build/config/BUILDCONFIG.gn:133

### ubsan_default_options
Default [UndefinedBehaviorSanitizer](https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html)
options (before the `UBSAN_OPTIONS` environment variable is read at
runtime).  This can be set as a build argument to affect most "ubsan"
variants in $variants (which see), or overridden in $toolchain_args in
one of those variants.  This can be a list of strings or a single string.

Note that even if this is empty, programs in this build **cannot** define
their own `__ubsan_default_options` C function.  Instead, they can use a
sanitizer_extra_options() target in their `deps` and then any options
injected that way can override that option's setting in this list.

**Current value (from the default):** `["print_stacktrace=1", "halt_on_error=1"]`

From //zircon/public/gn/config/instrumentation/sanitizer_default_options.gni:40

### universal_variants

**Current value (from the default):**
```
[{
  configs = []
  name = "release"
  toolchain_args = {
  is_debug = false
}
}]
```

From //build/config/BUILDCONFIG.gn:884

### universe_package_labels
If you add package labels to this variable, the packages will be included
in the 'universe' package set, which represents all software that is
produced that is to be published to a package repository or to the SDK by
the build. The build system ensures that the universe package set includes
the base and cache package sets, which means you do not need to redundantly
include those labels in this variable.

**Current value for `target_cpu = "arm64"`:** `["//tools/net/device-finder:host", "//bundles:tools"]`

From //products/core.gni:85

**Overridden from the default:** `[]`

From //BUILD.gn:51

**Current value for `target_cpu = "x64"`:** `["//tools/net/device-finder:host", "//bundles:tools"]`

From //products/core.gni:85

**Overridden from the default:** `[]`

From //BUILD.gn:51

### unpack_debug_archives
To ensure that everything can be built without debug symbols present we
gate weather or not these are consumed on a build argument. When set,
unpack_debug_archives creates an additional build step that unpacks
debug archives in tar.bzip2 format into the .build-id directory

**Current value (from the default):** `false`

From //build/packages/prebuilt_package.gni:13

### update_kernels
(deprecated) List of kernel images to include in the update (OTA) package.
If no list is provided, all built kernels are included. The names in the
list are strings that must match the filename to be included in the update
package.

**Current value (from the default):** `[]`

From //build/images/args.gni:35

### use_cast_runner_canary
If true then the most recent canary version of the Cast Runner is used,
otherwise the most recently validated version is used.

**Current value (from the default):** `false`

From //src/cast/BUILD.gn:11

### use_ccache
Set to true to enable compiling with ccache

**Current value (from the default):** `false`

From //build/toolchain/ccache.gni:9

### use_chromium_canary
Set to use the most recent canary version of prebuilt Chromium components
otherwise the most recently validated version is used.

**Current value (from the default):** `false`

From //src/chromium/BUILD.gn:13

### use_goma
Set to true to enable distributed compilation using Goma.

**Current value (from the default):** `false`

From //build/toolchain/goma.gni:11

### use_lto
Use link time optimization (LTO).

**Current value (from the default):** `false`

From //build/config/lto/config.gni:7

### use_netstack3
If true, replaces netstack with netstack3.

When set as a GN argument, requires that sysmgr_config_warn_override be set
to true to avoid build failures. See that flag's documentation for details.

**Current value (from the default):** `false`

From //src/connectivity/network/BUILD.gn:10

### use_null_vulkan_on_host

Global arguments for whether we use a "null" Vulkan implementation on
host vulkan_executables and vulkan_tests, so that any attempt to create a
VkInstances or VkDevice will fail.

This argument will affect all vulkan_{executable/test} build targets.


**Current value (from the default):** `false`

From //src/lib/vulkan/build/config.gni:31

### use_prebuilt_ffmpeg
Use a prebuilt ffmpeg binary rather than building it locally.  See
//src/media/lib/ffmpeg/README.md for details.  This is ignored when
building in variant builds for which there is no prebuilt.  In that
case, ffmpeg is always built from source so as to be built with the
selected variant's config.  When this is false (either explicitly or in
a variant build) then //third_party/ffmpeg must be in the source tree,
which requires:
`jiri import -name integration third_party/ffmpeg https://fuchsia.googlesource.com/integration`

**Current value (from the default):** `true`

From //src/media/lib/ffmpeg/BUILD.gn:14

### use_scudo
TODO(davemoore): Remove this entire mechanism once standalone scudo is the
default (DNO-442)
Enable the [Scudo](https://llvm.org/docs/ScudoHardenedAllocator.html)
memory allocator.

**Current value (from the default):** `false`

From //build/config/scudo/scudo.gni:10

### use_swiftshader_vulkan_icd_on_host

Global arguments for whether we use the SwiftShader Vulkan ICD on host
vulkan_executables and vulkan_tests.

This argument will affect all vulkan_{executable/test} build targets and
it only works when use_null_vulkan_on_host is set to false.


**Current value (from the default):** `true`

From //src/lib/vulkan/build/config.gni:40

### use_thinlto
Use ThinLTO variant of LTO if use_lto = true.

**Current value (from the default):** `true`

From //build/config/lto/config.gni:10

### use_vbmeta
If true, then a vbmeta image will be generated for provided ZBI
and the paving script will pave vbmeta images to the target device.

**Current value (from the default):** `false`

From //build/images/vbmeta.gni:14

### use_vboot
Use vboot images

**Current value (from the default):** `false`

From //build/images/args.gni:10

### using_fuchsia_sdk
Only set in buildroots where targets configure themselves for use with the
Fuchsia SDK

**Current value (from the default):** `false`

From //build/fuchsia/sdk.gni:8

### vbmeta_a_partition

**Current value (from the default):** `""`

From //build/images/args.gni:80

### vbmeta_b_partition

**Current value (from the default):** `""`

From //build/images/args.gni:81

### vbmeta_r_partition

**Current value (from the default):** `""`

From //build/images/args.gni:82

### vendor_linting
Whether libraries under //vendor should be linted.

**Current value (from the default):** `false`

From //build/fidl/fidl_library.gni:13

### virtmagma_debug
Enable verbose logging in virtmagma-related code

**Current value (from the default):** `false`

From //src/graphics/lib/magma/include/virtio/virtmagma_debug.gni:7

### vulkan_host_runtime_dir

|vulkan_host_runtime_dir| is the path to Vulkan runtime libraries, which
contains prebuilt Vulkan loader, Vulkan layers, SwiftShader Vulkan ICD,
and descriptor files required to load the libraries.


**Current value (from the default):** `"//prebuilt/third_party/vulkan_runtime/linux-x64"`

From //src/lib/vulkan/build/config.gni:18

### vulkan_host_sdk_dir

|vulkan_host_sdk_dir| is the path to Vulkan SDK, which contains Vulkan
headers and sources to Vulkan loader, layers and tools.


**Current value (from the default):** `"//prebuilt/third_party/vulkansdk/linux/x86_64"`

From //src/lib/vulkan/build/config.gni:11

### vulkan_sdk

**Current value (from the default):** `""`

From //src/graphics/examples/vkprimer/common/common.gni:48

### warn_on_sdk_changes
Whether to only warn when an SDK has been modified.
If false, any unacknowledged SDK change will cause a build failure.

**Current value (from the default):** `false`

From //build/sdk/config.gni:11

### weave_build_legacy_wdm
Tells openweave to support legacy WDM mode.

**Current value (from the default):** `false`

From [//third_party/openweave-core/config.gni:29](https://fuchsia.googlesource.com/third_party/openweave-core/+/5e90e806969a8f3dfc3050c08561db11761a3e6b/config.gni#29)

### weave_build_warm
Tells openweave to build WARM libraries.

**Current value (from the default):** `true`

From [//third_party/openweave-core/config.gni:26](https://fuchsia.googlesource.com/third_party/openweave-core/+/5e90e806969a8f3dfc3050c08561db11761a3e6b/config.gni#26)

### weave_system_config_use_sockets
Tells openweave components to use bsd-like sockets.

**Current value (from the default):** `true`

From [//third_party/openweave-core/config.gni:7](https://fuchsia.googlesource.com/third_party/openweave-core/+/5e90e806969a8f3dfc3050c08561db11761a3e6b/config.gni#7)

### weave_with_nlfaultinjection
Tells openweave components to support fault injection.

**Current value (from the default):** `false`

From [//third_party/openweave-core/config.gni:20](https://fuchsia.googlesource.com/third_party/openweave-core/+/5e90e806969a8f3dfc3050c08561db11761a3e6b/config.gni#20)

### weave_with_verhoeff
Tells openweave to support Verhoeff checksum.

**Current value (from the default):** `true`

From [//third_party/openweave-core/config.gni:23](https://fuchsia.googlesource.com/third_party/openweave-core/+/5e90e806969a8f3dfc3050c08561db11761a3e6b/config.gni#23)

### wlancfg_config_type
Selects the wlan configuration type to use. Choices:
  "client" - client mode
  "ap" - access point mode
  "" (empty string) - no configuration

**Current value (from the default):** `"client"`

From //src/connectivity/wlan/wlancfg/BUILD.gn:18

### zbi_compression
Compression setting for ZBI "storage" items.
This can be "zstd", optionally followed by ".LEVEL"
where `LEVEL` can be an integer or "max".

**Current value (from the default):** `"zstd"`

From //build/unification/zbi/migrated_zbi.gni:12

### zedboot_cmdline_args
List of kernel command line arguments to bake into the Zedboot image.
See //docs/reference/kernel_cmdline.md and
[`zedboot_devmgr_config`](#zedboot_devmgr_config).

**Current value (from the default):** `[]`

From //build/images/zedboot/zedboot_args.gni:9

### zedboot_cmdline_files
Files containing additional kernel command line arguments to bake into
the Zedboot image.  The contents of these files (in order) come after any
arguments directly in [`zedboot_cmdline_args`](#zedboot_cmdline_args).
These can be GN `//` source pathnames or absolute system pathnames.

**Current value (from the default):** `[]`

From //build/images/zedboot/zedboot_args.gni:15

### zedboot_devmgr_config
List of arguments to populate /boot/config/devmgr in the Zedboot image.

**Current value (from the default):** `[]`

From //build/images/zedboot/zedboot_args.gni:18

### zircon_a_partition
Arguments to `fx flash` script (along with any `firmware_prebuilts` which
specify a partition).

If `fvm_partition` is provided, the flash script will flash the full OS,
recovery + Zircon + FVM + SSH keys. In this case, the bootloader must also
support `fastboot oem add-staged-bootloader-file ssh.authorized_keys`.

Otherwise, the script will flash the recovery image to all slots, which
doesn't require the FVM or SSH keys.

**Current value (from the default):** `""`

From //build/images/args.gni:77

### zircon_args
[Zircon GN build arguments](/docs/gen/zircon_build_arguments.md).
The default passes through GOMA/ccache settings and
[`select_variant`](#select_variant) shorthand selectors.
**Only set this if you want to wipe out all the defaults that
propagate from Fuchsia GN to Zircon GN.**  The default value
folds in [`zircon_extra_args`](#zircon_extra_args), so usually
it's better to just set `zircon_extra_args` and leave `zircon_args` alone.
Any individual Zircon build argument set in `zircon_extra_args` will
silently clobber the default value shown here.

**Current value (from the default):**
```
{
  default_deps = ["//:legacy-arm64", "//:legacy_host_targets-linux-x64", "//:legacy_unification-arm64", "//tools:all-hosts"]
  disable_kernel_pci = false
  goma_dir = "/b/s/w/ir/k/prebuilt/third_party/goma/linux-x64"
  output_gsym = false
  rustc_version_string = "0v1jaeyeb9K3EGyl_O56bQ02Nt1CCd3_JeANRsXANBUC"
  use_ccache = false
  use_goma = false
  variants = []
  zbi_compression = "zstd"
  zx_fidl_trace_level = 0
}
```

From //BUILD.gn:110

### zircon_asserts

**Current value (from the default):** `true`

From //build/config/fuchsia/BUILD.gn:198

### zircon_b_partition

**Current value (from the default):** `""`

From //build/images/args.gni:78

### zircon_build_root

**Current value (from the default):** `"//zircon"`

From //src/graphics/lib/magma/gnbuild/magma.gni:18

### zircon_compdb_filter
Compilation database filter. Gets passed to --export-compile-commands=<filter>.

**Current value (from the default):** `"default"`

From //BUILD.gn:80

### zircon_extra_args
[Zircon GN build arguments](/docs/gen/zircon_build_arguments.md).
This is included in the default value of [`zircon_args`](#zircon_args) so
you can set this to add things there without wiping out the defaults.
When you set `zircon_args` directly, then this has no effect at all.
Arguments you set here override any arguments in the default
`zircon_args`.  There is no way to append to a value from the defaults.
Note that for just setting simple (string-only) values in Zircon GN's
[`variants`](/docs/gen/zircon_build_arguments.md#variants), the
default [`zircon_args`](#zircon_args) uses a `variants` value derived from
[`select_variant`](#select_variant) so for simple cases there is no need
to explicitly set Zircon's `variants` here.

**Current value (from the default):** `{ }`

From //BUILD.gn:69

### zircon_extra_deps
Additional Zircon GN labels to include in the Zircon build.

**Current value (from the default):** `[]`

From //BUILD.gn:73

### zircon_optimize
Zircon optimization level. Same acceptable values as `optimize`.
Note that this will be ignored, in favor of the global `optimize` variable
if the latter is one of: "none", "sanitizer", "profile" or "size".

**Current value (from the default):** `"default"`

From //build/config/zircon/levels.gni:18

### zircon_r_partition

**Current value (from the default):** `""`

From //build/images/args.gni:79

### zircon_toolchain
*This should never be set as a build argument.*
It exists only to be set in `toolchain_args`.
For Zircon toolchains, this will be a scope whose schema
is documented in //build/toolchain/zircon/zircon_toolchain.gni.
For all other toolchains, this will be false.

This allows testing for a Zircon-specific toolchain with:

  if (zircon_toolchain != false) {
    // code path for Zircon-specific toolchains
  } else {
    // code path for non-Zircon ones.
  }

**Current value (from the default):** `false`

From //build/config/BUILDCONFIG.gn:150

### zircon_tracelog
Where to emit a tracelog from Zircon's GN run. No trace will be produced if
given the empty string. Path can be source-absolute or system-absolute.

**Current value (from the default):** `""`

From //BUILD.gn:77

### zvb_partition_name
Partition name from where image will be verified

**Current value (from the default):** `"zircon"`

From //build/images/vbmeta.gni:36

### zx_assert_level
Controls which asserts are enabled.

`ZX_ASSERT` is always enabled.

* 0 disables standard C `assert()` and `ZX_DEBUG_ASSERT`.
* 1 disables `ZX_DEBUG_ASSERT`. Standard C `assert()` remains enabled.
* 2 enables all asserts.

**Current value (from the default):** `2`

From //build/config/zircon/levels.gni:13

## `target_cpu = "arm64"`

### amlogic_decoder_firmware_path
Path to the amlogic decoder firmware file. Overrides the default in the build.

**Current value (from the default):** `""`

From //src/media/drivers/amlogic_decoder/BUILD.gn:12

### arm_float_abi
The ARM floating point mode. This is either the string "hard", "soft", or
"softfp". An empty string means to use the default one for the
arm_version.

**Current value (from the default):** `""`

From //build/config/arm.gni:20

### arm_optionally_use_neon
Whether to enable optional NEON code paths.

**Current value (from the default):** `false`

From //build/config/arm.gni:31

### arm_tune
The ARM variant-specific tuning mode. This will be a string like "armv6"
or "cortex-a15". An empty string means to use the default for the
arm_version.

**Current value (from the default):** `""`

From //build/config/arm.gni:25

### arm_use_neon
Whether to use the neon FPU instruction set or not.

**Current value (from the default):** `true`

From //build/config/arm.gni:28

### arm_version

**Current value (from the default):** `8`

From //build/config/arm.gni:12

## `target_cpu = "x64"`

### acpica_debug_output
Enable debug output in the ACPI library (used by the ACPI bus driver).

**Current value (from the default):** `false`

From [//third_party/acpica/BUILD.gn:9](https://fuchsia.googlesource.com/third_party/acpica/+/43a1af390c18b298e0bf1a081cbb2f608a91cf98/BUILD.gn#9)

