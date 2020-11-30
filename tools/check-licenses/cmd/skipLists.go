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
	"src/devices/tools/banjo/src/backends/templates",                     // Skip template directories
}

var additionalSkipFiles = []string{
	"src/connectivity/network/netstack/dns/servers_config_test.go",
	"third_party/rust_crates/README.md",  // TODO(b/172586985): Remove once completed.
	"third_party/dart-pkg/pub/README.md", // TODO(b/172586985): Remove once completed.
}
