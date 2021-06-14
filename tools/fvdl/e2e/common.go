// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package e2e

import (
	"errors"
	"log"
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
	runtimeDir       string
	setupOnce        sync.Once
)

func setUp(t *testing.T, intree bool) {
	setupOnce.Do(func() {
		ex, err := os.Executable()
		if err != nil {
			t.Fatal(err)
		}
		exDir := filepath.Dir(ex)

		runtimeDir = e2etest.FindDirFromDir(exDir, "fvdl_test_runtime_deps")
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
		if emulatorPath = e2etest.FindFileFromDir(filepath.Join(runtimeDir, "aemu"), "emulator"); emulatorPath == "" {
			t.Fatalf("Cannot find emulator binary from %q", runtimeDir)
		}
		if grpcwebproxyPath = e2etest.FindFileFromDir(filepath.Join(runtimeDir, "grpcwebproxy"), "grpcwebproxy"); grpcwebproxyPath == "" {
			t.Fatalf("Cannot find grpcwebproxy binary from %q", runtimeDir)
		}
		fvdl = filepath.Join(hostToolsDir, "fvdl")
		if _, err := os.Stat(fvdl); os.IsNotExist(err) {
			t.Fatal(err)
		}
		if intree {
			fuchsiaBuildDir = filepath.Join(runtimeDir, "images")
			if _, err := os.Stat(fuchsiaBuildDir); os.IsNotExist(err) {
				t.Fatalf("Invalid fuchsia build dir %q err: %s", fuchsiaBuildDir, err)
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
			packages := filepath.Join(fuchsiaBuildDir, "packages.tar.gz")
			if _, err := os.Stat(packages); os.IsNotExist(err) {
				t.Fatal(err)
			}
			if err := os.Mkdir(filepath.Join(runtimeDir, ".jiri_root"), 0o755); err != nil && !os.IsExist(err) {
				t.Fatal(err)
			}
			e2etest.ExtractPackage(t, packages, fuchsiaBuildDir)
			e2etest.GenerateFakeArgsFile(t, filepath.Join(fuchsiaBuildDir, "args.gn"))
		}
		e2etest.CreateSSHKeyPairFiles(t, runtimeDir)
	})
}

func launchEmuWithRetry(attempts int, cmd *exec.Cmd) error {
	if err := cmd.Run(); err != nil {
		var e *exec.ExitError
		if errors.As(err, &e) {
			// ExitCode == 1 means something went wrong with the launcher. Don't retry.
			if e.ExitCode() == 1 {
				return err
			}
		}
		if attempts--; attempts > 0 {
			log.Printf("Retry launching emulator... got err %s", err)
			return launchEmuWithRetry(attempts, cmd)
		}
		return err
	}
	return nil
}

// runVDLWithArgs runs fvdl, if intree, use environment variables to set tools and image path.
// if not intree, images will be downloaded from GCS.
func runVDLWithArgs(t *testing.T, args []string, intree bool) string {
	testOut, ok := os.LookupEnv("FUCHSIA_TEST_OUTDIR")
	if !ok {
		testOut = t.TempDir()
	}
	if err := os.MkdirAll(filepath.Join(testOut, t.Name()), 0o755); err != nil {
		t.Fatal(err)
	}
	vdlOut := filepath.Join(testOut, t.Name(), "vdl_out")
	t.Logf("[test info] writing vdl output to %s", vdlOut)
	td := t.TempDir()
	cmd := exec.Command(fvdl, append(args, []string{
		"--vdl-output", vdlOut,
		"--emulator-log", filepath.Join(testOut, t.Name(), "emu_log"),
		"--amber-unpack-root", filepath.Join(td, "packages"),
	}...)...)
	if intree {
		cmd.Env = append(os.Environ(),
			"FUCHSIA_BUILD_DIR="+fuchsiaBuildDir,
			"FUCHSIA_ZBI_COMPRESSION=zstd",
			"HOST_OUT_DIR="+hostToolsDir,
			"IMAGE_FVM_RAW="+fvm,
			"IMAGE_QEMU_KERNEL_RAW="+kernel,
			"IMAGE_ZIRCONA_ZBI="+zbi,
			"PREBUILT_AEMU_DIR="+emulatorPath,
			"PREBUILT_GRPCWEBPROXY_DIR="+grpcwebproxyPath,
			"PREBUILT_VDL_DIR="+deviceLauncher,
		)
	} else {
		t.Logf("[test info] setting HOME to: %s", runtimeDir)
		// Set $HOME to runtimeDir so that fvdl can find the ssh key files, which are
		// expected in $HOME/.ssh/...
		cmd.Env = append(os.Environ(),
			"HOME="+runtimeDir,
		)
	}
	cmd.Dir = td
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr

	t.Cleanup(func() {
		killEmu(t, intree, vdlOut)
	})

	if err := launchEmuWithRetry(2, cmd); err != nil {
		t.Fatal(err)
	}
	return vdlOut
}

// killEmu shuts down fvdl with action kill command
func killEmu(t *testing.T, intree bool, vdlOut string) {
	t.Logf("[test info] killing fvdl using proto: %s", vdlOut)
	var cmd *exec.Cmd
	if intree {
		cmd = exec.Command(fvdl, []string{
			"kill", "--launched-proto", vdlOut}...)
		cmd.Env = append(os.Environ(),
			"FUCHSIA_BUILD_DIR="+fuchsiaBuildDir,
			"FUCHSIA_ZBI_COMPRESSION=zstd",
			"HOST_OUT_DIR="+hostToolsDir,
			"IMAGE_FVM_RAW="+fvm,
			"IMAGE_QEMU_KERNEL_RAW="+kernel,
			"IMAGE_ZIRCONA_ZBI="+zbi,
			"PREBUILT_AEMU_DIR="+emulatorPath,
			"PREBUILT_GRPCWEBPROXY_DIR="+grpcwebproxyPath,
			"PREBUILT_VDL_DIR="+deviceLauncher,
		)
	} else {
		cmd = exec.Command(fvdl, []string{
			"--sdk", "kill", "--launched-proto", vdlOut, "-d", filepath.Join(deviceLauncher, "device_launcher")}...)
	}
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	if err := cmd.Run(); err != nil {
		t.Errorf("shutting down fvdl errored: %w", err)
	}
}
