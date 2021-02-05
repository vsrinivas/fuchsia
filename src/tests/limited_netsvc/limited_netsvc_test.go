// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"crypto/rand"
	"encoding/hex"
	"net"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"go.fuchsia.dev/fuchsia/src/testing/emulator"
	"go.fuchsia.dev/fuchsia/tools/net/netutil"
	"go.fuchsia.dev/fuchsia/tools/net/tftp"
	fvdpb "go.fuchsia.dev/fuchsia/tools/virtual_device/proto"
)

// The default nodename given to an target with the default QEMU MAC address.
const defaultNodename = "swarm-donut-petri-acre"

func toolPath(t *testing.T, name string) string {
	ex, err := os.Executable()
	if err != nil {
		t.Fatal(err)
		return ""
	}
	exPath := filepath.Dir(ex)
	return filepath.Join(exPath, "test_data", "limited_netsvc", name)
}

func cmdWithTimeout(t *testing.T, shouldWork bool, name string, arg ...string) {
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	cmd := exec.CommandContext(ctx, name, arg...)

	out, err := cmd.Output()

	if shouldWork && ctx.Err() == context.DeadlineExceeded {
		t.Errorf("%s timed out %s, err=%s", name, out, err)
	} else if shouldWork && err != nil {
		t.Errorf("%s failed %s, err=%s", name, out, err)
	} else if !shouldWork && err == nil {
		t.Errorf("%s succeeded but shouldn't have %s, err=%s", name, out, err)
	} else {
		expected := "succeeded"
		if !shouldWork {
			expected = "failed"
		}
		t.Logf("%s %s as expected", name, expected)
	}
}

func attemptNetcp(t *testing.T, i *emulator.Instance, shouldWork bool) {
	cmdWithTimeout(
		t, shouldWork,
		toolPath(t, "netcp"),
		defaultNodename+":/boot/kernel/vdso/full",
		"/tmp/vdso")
}

func randomTokenAsString(t *testing.T) string {
	b := [32]byte{}
	if _, err := rand.Read(b[:]); err != nil {
		t.Fatal(err)
	}
	return hex.EncodeToString(b[:])
}

func attemptNetruncmd(t *testing.T, i *emulator.Instance, shouldWork bool) {
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	name := toolPath(t, "netruncmd")
	tokenFromNetruncmd := randomTokenAsString(t)
	cmd := exec.CommandContext(ctx, name, defaultNodename, tokenFromNetruncmd)

	if err := cmd.Run(); err != nil {
		t.Errorf("%s failed to run? err=%s", name, err)
	}

	time.Sleep(time.Second)
	tokenFromSerial := randomTokenAsString(t)
	if err := i.RunCommand("echo '" + tokenFromSerial + "'"); err != nil {
		t.Fatal(err)
	}

	if shouldWork {
		// If netruncmd works, we should get the token echo'd by netruncmd first,
		// then the terminator (which is sent over serial).
		if err := i.WaitForLogMessage(tokenFromNetruncmd); err != nil {
			t.Fatal(err)
		}
		if err := i.WaitForLogMessage(tokenFromSerial); err != nil {
			t.Fatal(err)
		}
		t.Logf("%s succeeded as expected", name)
	} else {
		// If netruncmd isn't working, we must not see the netruncmd token, and
		// should only get the serial token.
		if err := i.WaitForLogMessageAssertNotSeen(tokenFromSerial, tokenFromNetruncmd); err != nil {
			t.Fatal(err)
		}
		t.Logf("%s failed as expected", name)
	}
}

func attemptNetls(t *testing.T, i *emulator.Instance, shouldWork bool) {
	cmdWithTimeout(t, shouldWork, toolPath(t, "netls"))
}

func attemptNetaddr(t *testing.T, i *emulator.Instance, shouldWork bool) {
	cmdWithTimeout(t, shouldWork, toolPath(t, "netaddr"))
}

func attemptTftp(t *testing.T, i *emulator.Instance, shouldWork bool) {
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	addr, err := netutil.GetNodeAddress(ctx, defaultNodename)
	if err != nil {
		t.Fatal(err)
		return
	}

	tftp, err := tftp.NewClient(&net.UDPAddr{
		IP:   addr.IP,
		Port: tftp.ClientPort,
		Zone: addr.Zone,
	}, 0, 0)
	if err != nil {
		t.Fatal(err)
		return
	}

	reader := strings.NewReader("123456789")
	writeErr := tftp.Write(ctx, "/tmp/test", reader, reader.Size())
	if shouldWork {
		if writeErr == nil {
			t.Log("tftp write succeeded as expected")
		} else {
			t.Fatalf("tftp write failed, but expected to succeed: %s", err)
		}
	} else {
		if writeErr == nil {
			t.Fatal("tftp write expected to fail, but succeeded")
		} else {
			t.Logf("tftp write failed as expected: %s", writeErr)
		}
	}
}

func attemptLoglistener(t *testing.T, i *emulator.Instance, shouldWork bool) {
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	name := toolPath(t, "loglistener")
	cmd := exec.CommandContext(ctx, name, defaultNodename)

	out, err := cmd.Output()
	if err != nil && err.Error() != "signal: killed" {
		t.Errorf("%s: %s", name, err)
		return
	}

	if shouldWork {
		if !strings.Contains(string(out), "netsvc: start") {
			t.Errorf("%s didn't find 'netsvc: start' in retrieved log: %s, err=%s", name, out, err)
		} else {
			t.Logf("%s succeeded as expected", name)
		}
	} else {
		// Should only get the "listening..." line, but no other output.
		if len(strings.Split(string(out), "\n")) != 1 {
			t.Errorf("%s incorrectly received log\n", name)
			t.Errorf("got: %s", string(out))
		} else {
			t.Logf("%s failed as expected", name)
		}
	}
}

func setupQemu(t *testing.T, appendCmdline []string, modeString string) *emulator.Instance {
	exDir := execDir(t)
	distro, err := emulator.UnpackFrom(filepath.Join(exDir, "test_data"), emulator.DistributionParams{
		Emulator: emulator.Qemu,
	})
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() {
		if err = distro.Delete(); err != nil {
			t.Error(err)
		}
	})
	arch, err := distro.TargetCPU()
	if err != nil {
		t.Fatal(err)
	}

	device := emulator.DefaultVirtualDevice(string(arch))
	device.KernelArgs = append(device.KernelArgs, appendCmdline...)

	// Note: To run this test locally on linux, you must create the TAP interface:
	// $ sudo ip tuntap add mode tap qemu
	device.Hw.NetworkDevices = append(device.Hw.NetworkDevices, &fvdpb.Netdev{
		Id:     "qemu",
		Kind:   "tap",
		Device: &fvdpb.Device{Model: "virtio-net-pci"},
	})
	i, err := distro.Create(device)
	if err != nil {
		t.Fatal(err)
	}

	if err = i.Start(); err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() {
		if err = i.Kill(); err != nil {
			t.Error(err)
		}
	})

	// Make sure netsvc in expected mode.
	if err = i.WaitForLogMessage("netsvc: running in " + modeString + " mode"); err != nil {
		t.Fatal(err)
	}

	// Make sure netsvc is booted.
	if err = i.WaitForLogMessage("netsvc: start"); err != nil {
		t.Fatal(err)
	}

	return i
}

func TestNetsvcAllFeatures(t *testing.T) {
	cmdline := []string{"netsvc.all-features=true"}
	i := setupQemu(t, cmdline, "full")

	// Setting all-features to true means netsvc will work normally, and all
	// features should work.
	attemptLoglistener(t, i, true)
	attemptNetaddr(t, i, true)
	attemptNetcp(t, i, true)
	attemptNetls(t, i, true)
	attemptNetruncmd(t, i, true)
	attemptTftp(t, i, true)
}

func TestNetsvcAllFeaturesWithNodename(t *testing.T) {
	cmdline := []string{"netsvc.all-features=true", "zircon.nodename=" + defaultNodename}
	i := setupQemu(t, cmdline, "full")

	// Setting all-features to true means netsvc will work normally, and all
	// features should work.
	attemptLoglistener(t, i, true)
	attemptNetaddr(t, i, true)
	attemptNetcp(t, i, true)
	attemptNetls(t, i, true)
	attemptNetruncmd(t, i, true)
	attemptTftp(t, i, true)
}

func TestNetsvcLimited(t *testing.T) {
	cmdline := []string{"netsvc.all-features=false"}
	i := setupQemu(t, cmdline, "limited")

	// Explicitly setting all-features to false should be the same as default, and
	// most operations should fail.
	attemptLoglistener(t, i, false)
	attemptNetaddr(t, i, true)
	attemptNetcp(t, i, false)
	attemptNetls(t, i, true)
	attemptNetruncmd(t, i, false)
	attemptTftp(t, i, false)
}

func execDir(t *testing.T) string {
	ex, err := os.Executable()
	if err != nil {
		t.Fatal(err)
		return ""
	}
	return filepath.Dir(ex)
}
