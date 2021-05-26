// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package e2e

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"sync"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/fvdl/e2e/e2etest"
)

var (
	emulatorPath     string
	fvdl             string
	deviceLauncher   string
	grpcwebproxyPath string
	fuchsiaBuildDir  string
	hostToolsDir     string
	fvm              string
	zbi              string
	kernel           string
	runSetup         sync.Once
)

func setUp(t *testing.T) {
	runSetup.Do(func() {
		ex, err := os.Executable()
		if err != nil {
			t.Fatal(err)
		}
		exDir := filepath.Dir(ex)

		runtimeDir := e2etest.FindDirFromDir(exDir, "fvdl_test_runtime_deps")
		if len(runtimeDir) == 0 {
			t.Fatalf("Cannot find fvdl_test_runtime_deps binary from %s", exDir)
		}
		t.Logf("[test info] fuchsia_build_dir %s", exDir)
		t.Logf("[test info] fvdl_test_runtime_deps %s", runtimeDir)
		deviceLauncher = filepath.Join(runtimeDir, "vdl")
		if _, err := os.Stat(deviceLauncher); os.IsNotExist(err) {
			t.Fatalf("Invalid vdl path %q err: %s", deviceLauncher, err)
		}
		hostToolsDir = filepath.Join(runtimeDir, "host_tools")
		if _, err := os.Stat(hostToolsDir); os.IsNotExist(err) {
			t.Fatalf("Invalid host tools dir %q err: %s", hostToolsDir, err)
		}
		fuchsiaBuildDir = filepath.Join(runtimeDir, "images")
		if _, err := os.Stat(fuchsiaBuildDir); os.IsNotExist(err) {
			t.Fatalf("Invalid fuchsia build dir %q err: %s", fuchsiaBuildDir, err)
		}
		if emulatorPath = e2etest.FindFileFromDir(filepath.Join(runtimeDir, "aemu"), "emulator"); emulatorPath == "" {
			t.Fatalf("Cannot find emulator binary from %q", runtimeDir)
		}
		if grpcwebproxyPath = e2etest.FindFileFromDir(filepath.Join(runtimeDir, "grpcwebproxy"), "grpcwebproxy"); grpcwebproxyPath == "" {
			t.Fatalf("Cannot find grpcwebproxy binary from %q", runtimeDir)
		}
		zbi = filepath.Join(fuchsiaBuildDir, "fuchsia.zbi")
		if _, err := os.Stat(zbi); os.IsNotExist(err) {
			t.Fatal(err)
		}
		fvm = filepath.Join(fuchsiaBuildDir, "fvm.blk")
		if _, err := os.Stat(fvm); os.IsNotExist(err) {
			t.Fatal(err)
		}
		kernel = filepath.Join(fuchsiaBuildDir, "multiboot.bin")
		if _, err := os.Stat(kernel); os.IsNotExist(err) {
			t.Fatal(err)
		}
		fvdl = filepath.Join(hostToolsDir, "fvdl")
		if _, err := os.Stat(fvdl); os.IsNotExist(err) {
			t.Fatal(err)
		}
		packages := filepath.Join(fuchsiaBuildDir, "packages.tar.gz")
		if _, err := os.Stat(packages); os.IsNotExist(err) {
			t.Fatal(err)
		}
		if err := os.Mkdir(filepath.Join(runtimeDir, ".jiri_root"), 0o755); err != nil && !os.IsExist(err) {
			t.Fatal(err)
		}
		e2etest.ExtractPackage(t, packages, fuchsiaBuildDir)
		e2etest.CreateSSHKeyPairFiles(t, runtimeDir)
		e2etest.GenerateFakeArgsFile(t, filepath.Join(fuchsiaBuildDir, "args.gn"))
	})
}

// runVDLInTreeWithArgs runs fvdl by setting tools path via environment variables.
func runVDLInTreeWithArgs(t *testing.T, args []string, checkPackageServer, checkProxy bool) {
	testOut, ok := os.LookupEnv("FUCHSIA_TEST_OUTDIR")
	if !ok {
		testOut = t.TempDir()
	}
	if err := os.MkdirAll(filepath.Join(testOut, t.Name()), 0o755); err != nil {
		t.Fatal(err)
	}
	vdlOut := filepath.Join(testOut, t.Name(), "vdl_out")
	t.Logf("[test info] writing vdl output to %s", vdlOut)
	cmd := exec.Command(fvdl, append(args, []string{
		"--vdl-output", vdlOut,
		"--emulator-log", filepath.Join(testOut, t.Name(), "emu_log"),
	}...)...)
	cmd.Env = append(os.Environ(),
		"HOST_OUT_DIR="+hostToolsDir,
		"PREBUILT_AEMU_DIR="+emulatorPath,
		"PREBUILT_VDL_DIR="+deviceLauncher,
		"PREBUILT_GRPCWEBPROXY_DIR="+grpcwebproxyPath,
		"IMAGE_FVM_RAW="+fvm,
		"IMAGE_QEMU_KERNEL_RAW="+kernel,
		"IMAGE_ZIRCONA_ZBI="+zbi,
		"FUCHSIA_BUILD_DIR="+fuchsiaBuildDir,
		"FUCHSIA_ZBI_COMPRESSION=zstd",
	)
	cmd.Dir = t.TempDir()
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr

	defer func() {
		if err := killEmu(fvdl, vdlOut); err != nil {
			t.Error(err)
		}
	}()

	if err := cmd.Run(); err != nil {
		t.Error(err)
	}
	pid := e2etest.GetProcessPID("Emulator", vdlOut)
	if len(pid) == 0 {
		t.Errorf("Cannot obtain Emulator info from vdl output: %s", vdlOut)
	} else {
		if !e2etest.IsEmuRunning(pid) {
			t.Error("Emulator is not running")
		}
	}
	if checkPackageServer {
		if process := e2etest.GetProcessPID("PackageServer", vdlOut); len(process) == 0 {
			t.Errorf("Cannot obtain PackageServer process from vdl output: %s", vdlOut)
		}
	}
	if checkProxy {
		if process := e2etest.GetProcessPID("grpcwebproxy", vdlOut); len(process) == 0 {
			t.Errorf("Cannot obtain grpcwebproxy process from vdl output: %s", vdlOut)
		}
		if port := e2etest.GetProcessPort("grpcwebproxy", vdlOut); len(port) == 0 {
			t.Errorf("Cannot obtain grpcwebproxy port from vdl output: %s", vdlOut)
		}
	}
}

// Shuts down fvdl with action kill command
func killEmu(fvdl, vdlOut string) error {
	args := []string{
		"kill",
		"--launched-proto", vdlOut,
	}
	cmd := exec.Command(fvdl, args...)
	cmd.Env = append(os.Environ(),
		"HOST_OUT_DIR="+hostToolsDir,
		"PREBUILT_AEMU_DIR="+emulatorPath,
		"PREBUILT_VDL_DIR="+deviceLauncher,
		"PREBUILT_GRPCWEBPROXY_DIR="+grpcwebproxyPath,
		"IMAGE_FVM_RAW="+fvm,
		"IMAGE_QEMU_KERNEL_RAW="+kernel,
		"IMAGE_ZIRCONA_ZBI="+zbi,
		"FUCHSIA_BUILD_DIR="+fuchsiaBuildDir,
		"FUCHSIA_ZBI_COMPRESSION=zstd",
	)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("Shutting down fvdl errored: %s", err)
	}
	return nil
}

func TestStartFVDLInTree_GrpcWebProxy(t *testing.T) {
	setUp(t)
	runVDLInTreeWithArgs(t, []string{"start", "--nointeractive",
		"--nopackageserver", "--grpcwebproxy", "0", "--image-size", "10G"}, false, true)
}

func TestStartFVDLInTree_Headless_ServePackages_Tuntap(t *testing.T) {
	setUp(t)
	runVDLInTreeWithArgs(t, []string{"start", "--nointeractive",
		"--headless", "-N", "--image-size", "10G"}, true, false)
}
