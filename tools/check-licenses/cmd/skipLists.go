// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

var additionalSkipDirs = []string{
	// Directories that can always be skipped, all across the repository.
	"examples",
	"out",
	"vendor",
	"build/test",
	"garnet/examples",
	"garnet/public/lib/fostr/test",
	"prebuilt/ml/tests",
	"prebuilt/third_party", // TODO(jcecil): remove in a moment
	"third_party/glfw/examples",
	"third_party/glfw/tests",
	"prebuilt/third_party/go/linux-x64/pkg/linux_amd64/cmd/vendor",
	"prebuilt/third_party/go/linux-x64/pkg/linux_amd64/vendor",
	"prebuilt/third_party/go/linux-x64/src/cmd/vendor",
	"prebuilt/third_party/go/linux-x64/src/go/build/testdata",
	"prebuilt/third_party/go/linux-x64/src/vendor",
	"prebuilt/vendor",
	"scripts/fxtest/test",
	"scripts/sdk/gn/test_project/tests",
	"sdk/cts/tests",
	"sdk/dart/fuchsia_inspect/test",
	"sdk/lib/inspect/contrib/cpp/tests",
	"sdk/testing/sl4f/client/test",
	"src/connectivity/telephony/lib/qmi-protocol/tests",
	"src/connectivity/wlan/testing/hw-sim/BUILD.gn",
	"src/devices/sysmem/tests",
	"src/experiences/examples",
	"src/media/audio/examples",
	"src/security/fcrypto/test",
	"src/sys/component_manager/src/elf_runner/tests",
	"src/sys/component_manager/testing",
	"src/sys/component_manager/tests",
	"src/sys/lib/fuchsia-bootfs/testdata",
	"src/sys/pkg/bin/pm/examples",
	"src/sys/pkg/testing",
	"src/sys/pkg/tests",
	"src/tests",
	"src/ui/scenic/lib/input/tests",
	"third_party/expat/testdata",
	"third_party/go/src/cmd/vendor",
	"third_party/go/src/go/build/testdata",
	"third_party/go/src/vendor",
	"third_party/lz4/examples",
	"third_party/rust_crates/vendor",
	"tools/debug/elflib/testdata",
	"tools/debug/symbolize/testdata",
	"tools/fidl/fidlc/testdata",
	"tools/fidl/fidldoc/src/templates/markdown/testdata",
	"tools/fidl/gidl/mixer/testdata",
	"tools/testing",
	"zircon/kernel/lib/devicetree/tests",
	"zircon/system/ulib/bitmap/test",
	"zircon/system/ulib/elfload/test",
	"zircon/system/ulib/fs/test",
	"zircon/system/ulib/ktrace/test",
	"zircon/system/ulib/utf_conversion/test",

	// Skip template directories.
	"src/devices/tools/banjo/src/backends/templates",
	"src/devices/tools/fidlgen_banjo/src/backends/templates",

	// Skip generated localization files.
	"src/experiences/bin/simple_browser_internationalization/lib/localization",
	"src/experiences/session_shells/ermine/internationalization/lib/localization",

	// Skip flutter generated / cache directory
	"third_party/dart-pkg/git/flutter/bin/cache",

	// Skip "_latest" directories
	"prebuilt/third_party/chromedriver_latest",
	"prebuilt/third_party/chromium_tests_latest",
	"prebuilt/third_party/chromium_latest",

	// Skip check-licenses pattern texts.
	"tools/check-licenses/golden",

	// None of these binaries end up in Fuchsia images.
	"prebuilt/third_party/sysroot/linux",
	"prebuilt/touch",
	"prebuilt/audio",

	// Skip generated protocol buffers. These trip the checker because third_party is in the path.
	"third_party/grpc/src/core/ext/upb-generated/third_party",
	"third_party/grpc/src/core/ext/upbdefs-generated/third_party",

	// TODO(fxb/57392): zircon build unification
	"third_party/zstd",
	"third_party/lz4/programs",
	"third_party/lz4/tests",

	// TODO: Remove once completed
	"prebuilt/camera",                                    // b/169948153 -
	"prebuilt/connectivity/bluetooth/firmware/mediatek",  // b/178712290 -
	"prebuilt/third_party/aemu",                          // b/178682298 -
	"prebuilt/third_party/chromedriver",                  // b/180047878 -
	"prebuilt/third_party/chromium_tests",                // b/180047878 -
	"prebuilt/third_party/chromium",                      // b/180047878 -
	"prebuilt/third_party/gcc",                           // b/178682300 -
	"prebuilt/third_party/ovmf",                          // fxb/59350 -
	"prebuilt/third_party/qemu",                          // b/178682842 -
	"prebuilt/third_party/rust",                          // b/178714477 -
	"prebuilt/third_party/skia",                          // b/178714433 -
	"prebuilt/virtualization/packages/termina_guest",     // b/178857849 -
	"src/connectivity/wlan/drivers/third_party/mediatek", // b/173236643 -
	"third_party/catapult",                               // b/171586646 - To be deleted
	"third_party/cobalt_config",                          // b/178682768 -
	"third_party/crashpad/third_party/apple_cf",          // b/173233942 - To be deleted
	"third_party/crashpad/third_party/getopt",            // b/173234393 - To be deleted
	"third_party/grpc/third_party/cares",                 // b/173238234 -
	"third_party/libc-tests/third_party/nacl-tests",      // b/178682771
	"zircon/prebuilt/downloads/firmware/bluetooth",       // b/178734895 -
}

var additionalSkipFiles = []string{
	// Files that should always be skipped, all across the repository.
	".gitignore",
	"API_EVOLUTION.md",
	"CODE_OF_CONDUCT.md",
	"CONTRIBUTING.md",
	"LICENSE-APACHE.old",
	"LICENSE-MIT.old",
	"NOTICE.html",
	"SYNTAX.md",
	"TESTING.md",
	"all_selectors.txt",
	"build-changelist.txt",
	"cipd.gni",
	"code_coverage.pb.go",
	"getting_started.md",
	"glossary.md",
	"goldens.txt",
	"licenses.dart",
	"navbar.md",

	// unable to add copyright text to autogenerated code.
	"tools/femu-control/femu-grpc/proto/emulator_controller.pb.go",
	"tools/femu-control/femu-grpc/proto/emulator_controller_grpc.pb.go",

	// zstd cleanup
	"third_party/zstd/src/COPYING", // fxb/68624

	// TODO: Remove once completed.
	"third_party/rust_crates/README.md",
	"third_party/dart-pkg/pub/README.md",
	"prebuilt/third_party/rust/linux-x64/share/doc/rust/LICENSE-THIRD-PARTY",
	"prebuilt/third_party/rust_tools/linux-x64/share/doc/rust/LICENSE-THIRD-PARTY",
	"third_party/crashpad/third_party/glibc/elf/elf.h",
	"third_party/dart-pkg/pub/devtools/build/assets/NOTICES",
	"zircon/prebuilt/config.gni",                                                                                   // b/178734726
	"prebuilt/third_party/vulkansdk/linux/source/DirectXShaderCompiler/external/effcee/third_party/CMakeLists.txt", // b/178714418
}
