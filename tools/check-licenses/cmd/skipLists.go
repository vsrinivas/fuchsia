// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

var additionalSkipDirs = []string{
	"build/secondary/third_party/llvm",
	"prebuilt",                                           // TODO(b/172076124): Remove once completed.
	"prebuilt/third_party/flutter",                       // TODO(b/169676435): Remove once completed.
	"prebuilt/third_party/llvm",                          // TODO(b/172076113): Remove once completed.
	"prebuilt/third_party/ovmf",                          // TODO(fxb/59350): Remove once completed.
	"prebuilt/third_party/zlib",                          // TODO(b/172066115): Remove once completed.
	"prebuilt/virtualization/packages/termina_guest",     // TODO(b/171975485): Remove once completed.
	"src/connectivity/wlan/drivers/third_party/mediatek", // TODO(b/172586985): Remove once completed.
	"third_party/crashpad/third_party/apple_cf",          // TODO(b/173233942): Remove once completed.
	"third_party/crashpad/third_party/getopt",            // TODO(b/173234393): Remove once completed.
	"third_party/crashpad/third_party/xnu",               // TODO(b/173234731): Remove once completed.
	"third_party/catapult",                               // TODO(b/171586646): Remove once completed.
	"third_party/cobalt_config",                          // TODO(b/172586985): Remove once completed.
	"third_party/grpc/third_party/cares",                 // TODO(b/172586985): Remove once completed.
	"third_party/libc-tests/third_party/nacl-tests",      // TODO(b/172586985): Remove once completed.
	"third_party/rust_crates/compat",                     // TODO(b/172586985): Remove once completed.
	"third_party/tink/third_party/wycheproof",            // TODO(b/172586985): Remove once completed.
	"zircon/third_party/zstd",                            // TODO(b/171584331): Remove once completed.
	"src/devices/tools/banjo/src/backends/templates",     // Skip template directories
}

var additionalSkipFiles = []string{
	"src/connectivity/network/netstack/dns/servers_config_test.go",
	"third_party/rust_crates/README.md",  // TODO(b/172586985): Remove once completed.
	"third_party/dart-pkg/pub/README.md", // TODO(b/172586985): Remove once completed.
}
