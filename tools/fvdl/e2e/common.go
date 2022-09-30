// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package e2e

import (
	"context"
	"errors"
	"flag"
	"log"
	"os"
	"os/exec"
	"path/filepath"
	"sync"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/botanist/constants"
	"go.fuchsia.dev/fuchsia/tools/fvdl/e2e/e2etest"
	"go.fuchsia.dev/fuchsia/tools/lib/ffxutil"
)

var (
	aemuDir           = flag.String("aemu_dir", "", "Path to AEMU directory")
	grpcWebProxyDir   = flag.String("grpcwebproxy_dir", "", "Path to grpcwebproxy directory")
	deviceLauncherDir = flag.String("device_launcher_dir", "", "Path to device launcher directory")
	targetCPU         = flag.String("target_cpu", "", "Toolchain used to build Fuchsia image.")
	emulatorPath      string
	fvdl              string
	deviceLauncher    string
	grpcwebproxyPath  string
	fuchsiaBuildDir   string
	hostToolsDir      string
	ffx               string
	ffxInstance       *ffxutil.FFXInstance
	fvm               string
	zbi               string
	kernel            string
	runtimeDir        string
	amberFiles        string
	setupOnce         sync.Once
)

func setUp(t *testing.T, intree bool) {
	setupOnce.Do(func() {
		var err error

		deviceLauncher = *deviceLauncherDir
		if _, err := os.Stat(deviceLauncher); os.IsNotExist(err) {
			t.Fatalf("Invalid vdl path %q err: %s", deviceLauncher, err)
		}
		if emulatorPath = e2etest.FindFileFromDir(*aemuDir, "emulator"); emulatorPath == "" {
			t.Fatalf("Cannot find emulator binary from %q", runtimeDir)
		}
		if grpcwebproxyPath = e2etest.FindFileFromDir(*grpcWebProxyDir, "grpcwebproxy"); grpcwebproxyPath == "" {
			t.Fatalf("Cannot find grpcwebproxy binary from %q", runtimeDir)
		}

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
		hostToolsDir = filepath.Join(runtimeDir, "host_tools")
		if _, err := os.Stat(hostToolsDir); os.IsNotExist(err) {
			t.Fatalf("Invalid host tools dir %q err: %s", hostToolsDir, err)
		}
		fvdl = filepath.Join(hostToolsDir, "fvdl")
		if _, err := os.Stat(fvdl); os.IsNotExist(err) {
			t.Fatal(err)
		}
		ffx = filepath.Join(hostToolsDir, "ffx")
		if _, err := os.Stat(ffx); os.IsNotExist(err) {
			t.Fatal(err)
		}
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
		if err := e2etest.ExtractPackage(packages, fuchsiaBuildDir); err != nil {
			t.Fatal(err)
		}
		amberFiles = filepath.Join(fuchsiaBuildDir, "amber-files")
		if intree {
			if err := e2etest.GenerateFakeArgsFile(filepath.Join(fuchsiaBuildDir, "args.gn")); err != nil {
				t.Fatal(err)
			}
			if err := e2etest.GenerateFakeImagesJson(filepath.Join(fuchsiaBuildDir, "images.json")); err != nil {
				t.Fatal(err)
			}
		}
		if err := e2etest.CreateSSHKeyPairFiles(runtimeDir); err != nil {
			t.Fatal(err)
		}
	})
}

// runVDLWithArgs runs fvdl, if intree, use environment variables to set tools and image path.
// if not intree, images will be downloaded from GCS.
func runVDLWithArgs(ctx context.Context, t *testing.T, args []string, intree bool) string {
	testOut, ok := os.LookupEnv("FUCHSIA_TEST_OUTDIR")
	if !ok {
		testOut = t.TempDir()
	}
	if err := os.MkdirAll(filepath.Join(testOut, t.Name()), 0o755); err != nil {
		t.Fatal(err)
	}
	// Create a new isolated ffx instance.
	ffxInstance, err := ffxutil.NewFFXInstance(ctx, ffx, "", os.Environ(), os.Getenv(constants.NodenameEnvKey), os.Getenv(constants.SSHKeyEnvKey), testOut)
	if err != nil {
		t.Fatal(err)
	}
	vdlOut := filepath.Join(testOut, t.Name(), "vdl_out")
	t.Logf("[test info] writing vdl output to %s", vdlOut)
	td := t.TempDir()

	t.Cleanup(func() {
		killEmu(ctx, t, intree, vdlOut)
		if ffxInstance != nil {
			if err := ffxInstance.Stop(); err != nil {
				t.Logf("FFX didn't stop the running daemon %s", err)
			}
		}
	})

	maxTries := 2

	for tries := 0; tries < maxTries; tries++ {
		cmd := exec.CommandContext(
			ctx,
			fvdl,
			append(
				args,
				"--vdl-output", vdlOut,
				"--emulator-log", filepath.Join(testOut, t.Name(), "emu_log"),
				"--amber-unpack-root", filepath.Join(td, "packages"),
			)...,
		)
		cmd.Env = append(os.Environ(), ffxInstance.Env()...)
		if intree {
			cmd.Env = append(
				cmd.Env,
				"FUCHSIA_BUILD_DIR="+fuchsiaBuildDir,
				"FUCHSIA_ZBI_COMPRESSION=zstd",
				"HOST_OUT_DIR="+hostToolsDir,
				"PREBUILT_AEMU_DIR="+mustAbs(t, emulatorPath),
				"PREBUILT_GRPCWEBPROXY_DIR="+mustAbs(t, grpcwebproxyPath),
				"PREBUILT_VDL_DIR="+mustAbs(t, deviceLauncher),
				"FVDL_INVOKER=fvdl_e2e_test",
			)
		} else {
			t.Logf("[test info] setting HOME to: %s", runtimeDir)
			// Set $HOME to runtimeDir so that fvdl can find the ssh key files, which are
			// expected in $HOME/.ssh/...
			cmd.Env = append(cmd.Env, "HOME="+runtimeDir, "FVDL_INVOKER=fvdl_e2e_test")
		}
		cmd.Dir = td
		cmd.Stdout = os.Stdout
		cmd.Stderr = os.Stderr
		if err := cmd.Run(); err != nil {
			var e *exec.ExitError
			if errors.As(err, &e) {
				// ExitCode == 1 means something went wrong with the launcher. Don't retry.
				if e.ExitCode() == 1 {
					t.Fatal(err)
				}
			}
			if tries == maxTries-1 {
				t.Fatal(err)
			}
			log.Printf("Retry launching emulator... got err %s", err)
		} else {
			break
		}
	}
	return vdlOut
}

func mustAbs(t *testing.T, p string) string {
	t.Helper()
	abs, err := filepath.Abs(p)
	if err != nil {
		t.Fatalf("Failed to convert %q to absolute path: %v", p, err)
	}
	return abs
}

// killEmu shuts down fvdl with action kill command
func killEmu(ctx context.Context, t *testing.T, intree bool, vdlOut string) {
	t.Logf("[test info] killing fvdl using proto: %s", vdlOut)
	var cmd *exec.Cmd
	if intree {
		cmd = exec.CommandContext(ctx, fvdl, "kill", "--launched-proto", vdlOut)
		cmd.Env = append(
			os.Environ(),
			"FUCHSIA_BUILD_DIR="+fuchsiaBuildDir,
			"FUCHSIA_ZBI_COMPRESSION=zstd",
			"HOST_OUT_DIR="+hostToolsDir,
			"PREBUILT_AEMU_DIR="+mustAbs(t, emulatorPath),
			"PREBUILT_GRPCWEBPROXY_DIR="+mustAbs(t, grpcwebproxyPath),
			"PREBUILT_VDL_DIR="+mustAbs(t, deviceLauncher),
			"FVDL_INVOKER=fvdl_e2e_test",
		)
	} else {
		cmd = exec.CommandContext(
			ctx,
			fvdl,
			"--sdk",
			"kill",
			"--launched-proto",
			vdlOut,
			"-d",
			mustAbs(t, filepath.Join(deviceLauncher, "device_launcher")),
		)
		cmd.Env = append(os.Environ(), "FVDL_INVOKER=fvdl_e2e_test")
	}
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	if err := cmd.Run(); err != nil {
		t.Errorf("shutting down fvdl errored: %s", err)
	}
}
