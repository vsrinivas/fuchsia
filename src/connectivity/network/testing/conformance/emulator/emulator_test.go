// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package emulator

import (
	"crypto/rand"
	"crypto/rsa"
	"crypto/x509"
	"encoding/pem"
	"errors"
	"io/fs"
	"io/ioutil"
	"os"
	"path/filepath"
	"sync"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/lib/ffxutil"
	fvdpb "go.fuchsia.dev/fuchsia/tools/virtual_device/proto"
	"golang.org/x/crypto/ssh"
	"golang.org/x/net/context"
)

const RSA_KEY_NUM_BITS int = 2048
const PRIVATE_KEY_PERMISSIONS fs.FileMode = 0600
const PUBLIC_KEY_PERMISSIONS fs.FileMode = 0666

func TestEmulatorWorksWithFfx(t *testing.T) {
	var wg sync.WaitGroup
	defer wg.Wait()

	executablePath, err := os.Executable()
	if err != nil {
		// Use Error-then-return instead of Fatal so that deferred cleanup gets run.
		t.Error(err)
		return
	}
	hostOutDir, err := filepath.Abs(filepath.Dir(executablePath))
	if err != nil {
		t.Error(err)
		return
	}

	initrd := "network-conformance-base"
	nodename := "TestEmulatorWorksWithFfx-Nodename"

	// Note: To run this test locally on linux, you must create the TAP interface:
	// $ sudo ip tuntap add mode tap qemu
	// The qemu tap interface is assumed to exist on infra.
	netdevs := []*fvdpb.Netdev{{
		Id:   "qemu",
		Kind: "tap",
		Device: &fvdpb.Device{
			Model: "virtio-net-pci",
			Options: []string{
				"mac=00:00:00:00:00:0a",
				"addr=0a",
			},
		},
	}}

	privKey, err := rsa.GenerateKey(rand.Reader, RSA_KEY_NUM_BITS)
	if err != nil {
		t.Error(err)
		return
	}

	pemdata := pem.EncodeToMemory(
		&pem.Block{
			Type:  "RSA PRIVATE KEY",
			Bytes: x509.MarshalPKCS1PrivateKey(privKey),
		},
	)

	tempDir := t.TempDir()
	privKeyFilepath := filepath.Join(tempDir, "pkey")
	if err := os.WriteFile(privKeyFilepath, pemdata, PRIVATE_KEY_PERMISSIONS); err != nil {
		t.Error(err)
		return
	}

	pubKey, err := ssh.NewPublicKey(privKey.Public())
	if err != nil {
		t.Error(err)
		return
	}

	pubKeyData := ssh.MarshalAuthorizedKey(pubKey)
	pubKeyFilepath := filepath.Join(tempDir, "authorized_keys")
	if err := ioutil.WriteFile(pubKeyFilepath, pubKeyData, PUBLIC_KEY_PERMISSIONS); err != nil {
		t.Error(err)
		return
	}

	ctx, terminateEmulator := context.WithCancel(context.Background())
	defer terminateEmulator()

	i, err := NewQemuInstance(ctx, QemuInstanceArgs{
		Nodename:               nodename,
		Initrd:                 initrd,
		HostX64Path:            hostOutDir,
		HostPathAuthorizedKeys: pubKeyFilepath,
		NetworkDevices:         netdevs,
	})

	if err != nil {
		t.Error(err)
		return
	}

	sourceRootRelativeDir := filepath.Join(
		hostOutDir,
		"src",
		"connectivity",
		"network",
		"testing",
		"conformance",
		"emulator",
	)

	ffxPath := filepath.Join(sourceRootRelativeDir, "ffx")
	ffx, err := ffxutil.NewFFXInstance(
		ffxPath,
		"",
		os.Environ(),
		nodename,
		privKeyFilepath,
		tempDir,
	)
	if err != nil {
		t.Error(err)
		return
	}

	if err := ffx.SetLogLevel(ffxutil.Warn); err != nil {
		t.Error(err)
		return
	}

	ctx, closeDaemon := context.WithCancel(ctx)
	defer closeDaemon()

	wg.Add(1)
	go func() {
		defer wg.Done()
		if err := ffx.Run(ctx, "daemon", "start"); err != nil && !errors.Is(err, context.Canceled) {
			t.Error(err)
		}
	}()

	if err := ffx.TargetWait(ctx); err != nil {
		t.Error(err)
		return
	}

	terminateEmulator()
	if _, err := i.Wait(); err != nil {
		t.Error(err)
		return
	}
}
