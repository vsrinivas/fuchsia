// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package emulator

import (
	"crypto/rand"
	"crypto/rsa"
	"crypto/x509"
	"encoding/pem"
	"fmt"
	"io/fs"
	"io/ioutil"
	"os"
	"path/filepath"
	"sync"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/lib/ffxutil"
	"go.fuchsia.dev/fuchsia/tools/lib/jsonutil"
	fvdpb "go.fuchsia.dev/fuchsia/tools/virtual_device/proto"
	"go.uber.org/multierr"
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
		t.Fatal(err)
	}
	hostOutDir, err := filepath.Abs(filepath.Dir(executablePath))
	if err != nil {
		t.Fatal(err)
	}

	initrd := "network-conformance-base"
	nodename := "TestEmulatorWorksWithFfx-Nodename"

	// Note: To run this test locally on linux, you must create the TAP interface:
	// $ sudo ip tuntap add mode tap qemu; sudo ip link set dev qemu up
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
		t.Fatal(err)
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
		t.Fatal(err)
	}

	pubKey, err := ssh.NewPublicKey(privKey.Public())
	if err != nil {
		t.Fatal(err)
	}

	pubKeyData := ssh.MarshalAuthorizedKey(pubKey)
	pubKeyFilepath := filepath.Join(tempDir, "authorized_keys")
	if err := ioutil.WriteFile(pubKeyFilepath, pubKeyData, PUBLIC_KEY_PERMISSIONS); err != nil {
		t.Fatal(err)
	}

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	i, err := NewQemuInstance(ctx, QemuInstanceArgs{
		Nodename:               nodename,
		Initrd:                 initrd,
		HostX64Path:            hostOutDir,
		HostPathAuthorizedKeys: pubKeyFilepath,
		NetworkDevices:         netdevs,
	})

	if err != nil {
		t.Fatal(err)
	}

	emulatorDone := make(chan error)
	defer func() {
		for err := range emulatorDone {
			if err != nil {
				t.Error(err)
			}
		}
	}()

	// Ensure that cancel() is run before we try to drain the emulator error
	// channel.
	defer cancel()

	go func() {
		_, err := i.Wait()
		emulatorDone <- err
		close(emulatorDone)
	}()

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
		t.Fatal(err)
	}
	defer func() {
		if err := ffx.Stop(); err != nil {
			t.Error(err)
		}
	}()

	if err := ffx.SetLogLevel(ffxutil.Warn); err != nil {
		t.Fatal(err)
	}

	// It seems that this is necessary in order to get ffx to obey the config
	// when running the daemon, and that the --config flag passed by ffxutil
	// is not enough.
	// TODO(https://fxbug.dev/94420): migrate to http://go/ffx-isolate when
	// available.
	ffxEnvPath := filepath.Join(tempDir, "env.json")
	ffxConfigPath := ffx.ConfigPath
	if err := jsonutil.WriteToFile(ffxEnvPath, map[string]interface{}{
		"user":   ffxConfigPath,
		"build":  nil,
		"global": nil,
	}); err != nil {
		t.Fatal(multierr.Append(ffx.Stop(), fmt.Errorf(
			"error while writing ffx env file %s: %w",
			ffxEnvPath,
			err,
		)))
	}

	if err := ffx.RunWithTarget(
		ctx,
		"--env", ffxEnvPath,
		"target",
		"wait",
	); err != nil {
		t.Fatal(err)
	}
}
