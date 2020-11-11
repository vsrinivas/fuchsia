// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

var additionalSkipDirs = []string{
	"build/secondary/third_party/llvm",
	"prebuilt",                                                           // TODO(b/172076124): Remove once completed.
	"prebuilt/third_party/flutter",                                       // TODO(b/169676435): Remove once completed.
	"prebuilt/third_party/llvm",                                          // TODO(b/172076113): Remove once completed.
	"prebuilt/third_party/ovmf",                                          // TODO(fxb/59350): Remove once completed.
	"prebuilt/third_party/zlib",                                          // TODO(b/172066115): Remove once completed.
	"prebuilt/virtualization/packages/termina_guest",                     // TODO(b/171975485): Remove once completed.
	"src/connectivity/wlan/drivers/third_party/mediatek",                 // TODO(b/172586985): Remove once completed.
	"src/developer/system_monitor/bin/third_party",                       // TODO(b/172586985): Remove once completed.
	"third_party/android/device/generic/goldfish-opengl",                 // TODO(b/172586985): Remove once completed.
	"third_party/catapult",                                               // TODO(b/171586646): Remove once completed.
	"third_party/cobalt_config",                                          // TODO(b/172586985): Remove once completed.
	"third_party/crashpad/third_party/apple_cf",                          // TODO(b/172586985): Remove once completed.
	"third_party/crashpad/third_party/getopt",                            // TODO(b/172586985): Remove once completed.
	"third_party/crashpad/third_party/lss",                               // TODO(b/172586985): Remove once completed.
	"third_party/crashpad/third_party/mini_chromium",                     // TODO(b/172586985): Remove once completed.
	"third_party/crashpad/third_party/xnu",                               // TODO(b/172586985): Remove once completed.
	"third_party/dart-pkg/pub/characters/third_party/Unicode_Consortium", // TODO(b/172586985): Remove once completed.
	"third_party/dart-pkg/pub/characters/third_party/Wikipedia",          // TODO(b/172586985): Remove once completed.
	"third_party/gperftools/src/third_party",                             // TODO(b/172586985): Remove once completed.
	"third_party/grpc/third_party/cares",                                 // TODO(b/172586985): Remove once completed.
	"third_party/libc-tests/third_party/nacl-tests",                      // TODO(b/172586985): Remove once completed.
	"third_party/markupsafe",                                             // TODO(b/172586985): Remove once completed.
	"third_party/mesa",                                                   // TODO(b/172586985): Remove once completed.
	"third_party/openthread/third_party/build_gn",                        // TODO(b/172586985): Remove once completed.
	"third_party/openthread/third_party/jlink",                           // TODO(b/172586985): Remove once completed.
	"third_party/openthread/third_party/mbedtls",                         // TODO(b/172586985): Remove once completed.
	"third_party/openthread/third_party/microchip",                       // TODO(b/172586985): Remove once completed.
	"third_party/openthread/third_party/NordicSemiconductor",             // TODO(b/172586985): Remove once completed.
	"third_party/openthread/third_party/nxp",                             // TODO(b/172066115): Remove once completed.
	"third_party/openthread/third_party/openthread-test-driver",          // TODO(b/171816602): Remove once completed.
	"third_party/openthread/third_party/Qorvo",                           // TODO(b/172586985): Remove once completed.
	"third_party/openthread/third_party/silabs",                          // TODO(b/172586985): Remove once completed.
	"third_party/openthread/third_party/ti",                              // TODO(b/172066853): Remove once completed.
	"third_party/rust_crates/compat",                                     // TODO(b/172586985): Remove once completed.
	"third_party/rust_crates/tiny_mirrors",                               // TODO(b/172586985): Remove once completed.
	"third_party/tink/third_party/wycheproof",                            // TODO(b/172586985): Remove once completed.
	"third_party/vim",                                                    // TODO(b/172066343): Remove once completed.
	"third_party/zlib",                                                   // TODO(b/172069467): Remove once completed.
	"zircon/third_party/scudo",                                           // TODO(b/172586985): Remove once completed.
	"zircon/third_party/ulib",                                            // TODO(b/172586985): Remove once completed.
	"zircon/third_party/ulib/musl/third_party",                           // TODO(b/172066115): Remove once completed.
	"zircon/third_party/zstd",                                            // TODO(b/171584331): Remove once completed.
}

var additionalSkipFiles = []string{
	// TODO(b/172070492): Remove this list once completed.
	"src/devices/tools/banjo/src/backends/templates/c/body.h",
	"src/devices/tools/banjo/src/backends/templates/c/callback.h",
	"src/devices/tools/banjo/src/backends/templates/cpp/base_protocol.h",
	"src/devices/tools/banjo/src/backends/templates/cpp/example.h",
	"src/devices/tools/banjo/src/backends/templates/cpp/footer.h",
	"src/devices/tools/banjo/src/backends/templates/cpp/interface.h",
	"src/devices/tools/banjo/src/backends/templates/cpp/internal_decl.h",
	"src/devices/tools/banjo/src/backends/templates/cpp/internal_protocol.h",
	"src/devices/tools/banjo/src/backends/templates/cpp/internal_static_assert.h",
	"src/devices/tools/banjo/src/backends/templates/cpp/mock_expect.h",
	"src/devices/tools/banjo/src/backends/templates/cpp/mock.h",
	"src/devices/tools/banjo/src/backends/templates/cpp/protocol.h",
	"src/devices/tools/banjo/src/backends/templates/cpp/proto_transform.h",
	"src/devices/tools/banjo/src/backends/templates/c/protocol.h",
	"src/devices/tools/banjo/src/backends/templates/c/protocol_ops.h",
	"src/devices/tools/banjo/src/backends/templates/c/proto_transform.h",
	"src/devices/tools/banjo/src/backends/templates/c/struct.h",
	"src/devices/tools/banjo/src/backends/templates/rust/body.rs",
	"src/devices/tools/banjo/src/backends/templates/rust/enum.rs",
	"src/devices/tools/banjo/src/backends/templates/rust/protocol.rs",
	"src/devices/tools/banjo/src/backends/templates/rust/struct.rs",
	"src/devices/tools/banjo/src/backends/templates/rust/union.rs",
	"integration/infra/config/gen.sh",
	"integration/infra/update_test_durations.py",
	"scripts/codesize/lib/report.pb.dart",
	"src/camera/drivers/sensors/imx227/mipi_ccs_regs.h",
	"src/connectivity/ethernet/drivers/asix-88179/hooks.test.fidl",
	"src/connectivity/network/netstack/dhcp/dhcp_string.go",
	"src/connectivity/network/netstack/dns/servers_config_test.go",
	"src/connectivity/network/netstack/filter/filter_string.go",
	"src/developer/debug/zxdb/expr/BUILD.gn",
	"src/graphics/lib/magma/src/magma_util/platform/zircon/zircon_platform_sysmem_connection.cc",
	"src/lib/fdio/rust/scripts/bindgen-fdio.sh",
	"src/lib/fidl/c/coding_tables_tests/coding_tables_deps.test.fidl",
	"src/lib/fuzzing/fidl/BUILD.gn",
	"src/lib/icu/tools/extractor/main.cc",
	"src/lib/icu/tools/extractor/tz_version.cc",
	"src/lib/icu/tools/extractor/tz_version.h",
	"src/media/codec/codecs/sw/BUILD.gn",
	"src/security/policy/BUILD.gn",
	"src/security/scrutiny/lib/utils/ffi-bridge/chunked-decompressor.cc",
	"src/storage/fuchsia-fatfs/corpus/BUILD.gn",
	"src/sys/appmgr/fidl/fuchsia.appmgr/BUILD.gn",
	"src/ui/scenic/lib/gfx/engine/object_linker.cc",
	"src/ui/scenic/lib/gfx/resources/nodes/view_node.cc",
	"third_party/rust_crates/README.md",  // TODO(b/172586985): Remove once completed.
	"third_party/dart-pkg/pub/README.md", // TODO(b/172586985): Remove once completed.
	"tools/fidl/gidl/example/example.test.fidl",
	"tools/integration/bootstrap.sh",
	"tools/integration/cmd/fint/main.go",
	"tools/integration/cmd/fint/parse.go",
	"tools/integration/testsharder/preprocess.go",
	"tools/integration/testsharder/preprocess_test.go",
	"topaz/bin/fidl_compatibility_test/dart/pubspec.yaml",
	"topaz/bin/fidlgen_dart/regen.sh",
	"topaz/public/dart/composition_delegate/lib/src/internal/layout_logic/_layout_logic.dart",
	"topaz/tools/download_dev_sdk.py",
	"topaz/tools/utils.py",
	"zircon/kernel/dev/coresight/BUILD.gn",
	"zircon/kernel/dev/coresight/BUILD.zircon.gn",
	"zircon/kernel/kernel/thread_test.cc",
	"zircon/kernel/lib/libc/string/memchr.c",
	"zircon/kernel/lib/libc/string/memset.c",
	"zircon/kernel/lib/libc/string/strlcat.c",
	"zircon/kernel/lib/libc/string/strlcpy.c",
	"zircon/kernel/lib/libc/string/strrchr.c",
	"zircon/prebuilt/config.gni",
	"zircon/prebuilt/downloads/firmware/bluetooth/mt7668/mt7668_patch_e2_hdr.bin",
	"zircon/public/gn/migrated_targets.gni",
	"zircon/public/lib/cksum/BUILD.gn",
	"zircon/public/lib/fbl/BUILD.gn",
	"zircon/public/lib/zbi/BUILD.gn",
	"zircon/public/lib/zx/BUILD.gn",
	"zircon/public/sysroot/populate_sysroot_headers.py",
	"zircon/system/ulib/cobalt-client/BUILD.gn",
	"zircon/system/ulib/zbitl/include/lib/zbitl/error_stdio.h",
	"zircon/system/ulib/zbitl/include/lib/zbitl/error_string.h",
	"zircon/system/ulib/zircon/zx_ticks_per_second.cc",
	"zircon/tools/merkleroot/merkleroot.cc",
}
