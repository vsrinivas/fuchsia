// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"crypto/rand"
	"fmt"
	"net"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"go.fuchsia.dev/fuchsia/src/testing/qemu"
	"go.fuchsia.dev/fuchsia/tools/net/netutil"
	"go.fuchsia.dev/fuchsia/tools/net/tftp"
)

// The default nodename given to an target with the default QEMU MAC address.
const defaultNodename = "swarm-donut-petri-acre"

func zbiPath(t *testing.T) string {
	ex, err := os.Executable()
	if err != nil {
		t.Fatal(err)
		return ""
	}
	exPath := filepath.Dir(ex)
	return filepath.Join(exPath, "../fuchsia.zbi")
}

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

func attemptNetcp(t *testing.T, i *qemu.Instance, shouldWork bool) {
	cmdWithTimeout(
		t, shouldWork,
		toolPath(t, "netcp"),
		defaultNodename+":/boot/kernel/vdso/full",
		"/tmp/vdso")
}

func randomTokenAsString() string {
	b := make([]byte, 32)
	_, err := rand.Read(b)
	if err != nil {
		panic(err)
	}

	ret := ""
	for i := 0; i < 32; i++ {
		ret += fmt.Sprintf("%x", b[i])
	}
	return ret
}

func attemptNetruncmd(t *testing.T, i *qemu.Instance, shouldWork bool) {
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	name := toolPath(t, "netruncmd")
	tokenFromNetruncmd := randomTokenAsString()
	cmd := exec.CommandContext(ctx, name, defaultNodename, tokenFromNetruncmd)

	err := cmd.Run()
	if err != nil {
		t.Errorf("%s failed to run? err=%s", name, err)
	}

	time.Sleep(time.Second)
	tokenFromSerial := randomTokenAsString()
	i.RunCommand("echo '" + tokenFromSerial + "'")

	if shouldWork {
		// If netruncmd works, we should get the token echo'd by netruncmd first,
		// then the terminator (which is sent over serial).
		i.WaitForLogMessage(tokenFromNetruncmd)
		i.WaitForLogMessage(tokenFromSerial)
		t.Logf("%s succeeded as expected", name)
	} else {
		// If netruncmd isn't working, we must not see the netruncmd token, and
		// should only get the serial token.
		i.WaitForLogMessageAssertNotSeen(tokenFromSerial, tokenFromNetruncmd)
		t.Logf("%s failed as expected", name)
	}
}

func attemptNetls(t *testing.T, i *qemu.Instance, shouldWork bool) {
	cmdWithTimeout(t, shouldWork, toolPath(t, "netls"))
}

func attemptNetaddr(t *testing.T, i *qemu.Instance, shouldWork bool) {
	cmdWithTimeout(t, shouldWork, toolPath(t, "netaddr"))
}

func attemptTftp(t *testing.T, i *qemu.Instance, shouldWork bool) {
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
	})
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

func attemptLoglistener(t *testing.T, i *qemu.Instance, shouldWork bool) {
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

func setupQemu(t *testing.T, appendCmdline string, modeString string) (*qemu.Instance, func()) {
	distro, err := qemu.Unpack()
	if err != nil {
		t.Fatal(err)
	}
	arch, err := distro.TargetCPU()
	if err != nil {
		t.Fatal(err)
	}

	i := distro.Create(qemu.Params{
		Arch:          arch,
		ZBI:           zbiPath(t),
		AppendCmdline: appendCmdline,
		Networking:    true,
	})

	i.Start()
	if err != nil {
		t.Fatal(err)
	}

	// Make sure netsvc in expected mode.
	i.WaitForLogMessage("netsvc: running in " + modeString + " mode")

	// Make sure netsvc is booted.
	i.WaitForLogMessage("netsvc: start")

	return i, func() {
		i.Kill()
		distro.Delete()
	}
}

func TestNetsvcAllFeatures(t *testing.T) {
	i, cleanup := setupQemu(t, "netsvc.all-features=true", "full")
	defer cleanup()

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
	i, cleanup := setupQemu(t, "netsvc.all-features=false", "limited")
	defer cleanup()

	// Explicitly setting all-features to false should be the same as default, and
	// most operations should fail.
	attemptLoglistener(t, i, false)
	attemptNetaddr(t, i, true)
	attemptNetcp(t, i, false)
	attemptNetls(t, i, true)
	attemptNetruncmd(t, i, false)
	attemptTftp(t, i, false)
}
