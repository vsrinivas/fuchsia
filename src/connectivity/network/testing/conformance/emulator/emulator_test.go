// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package emulator

import (
	"bytes"
	"crypto/rand"
	"crypto/rsa"
	"crypto/x509"
	"encoding/json"
	"encoding/pem"
	"io"
	"io/fs"
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

const NETWORK_TEST_REALM_COMPONENT_NAME = "net-test-realm-controller"

const NETWORK_TEST_REALM_TEST_COLLECTION_MONIKER = "core/network/test-components"

const NETWORK_TEST_REALM_MONIKER = "/" + NETWORK_TEST_REALM_TEST_COLLECTION_MONIKER + ":" + NETWORK_TEST_REALM_COMPONENT_NAME
const NETWORK_TEST_REALM_URL = "fuchsia-pkg://fuchsia.com/network-test-realm#meta/controller.cm"

const NETWORK_TEST_REALM_COMPONENT_SELECTOR = NETWORK_TEST_REALM_TEST_COLLECTION_MONIKER + "\\:" + NETWORK_TEST_REALM_COMPONENT_NAME
const NETSTACK_VERSION = "v2"

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
	if err := os.WriteFile(pubKeyFilepath, pubKeyData, PUBLIC_KEY_PERMISSIONS); err != nil {
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
		ctx,
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
		// Just log errors since ffx.Stop() is expected to return a DeadlineExceeded
		// when the daemon takes longer than usual to shut down (which is not
		// actionable for us).
		if err := ffx.Stop(); err != nil {
			t.Logf("ffx.Stop() = %s", err)
		}
	}()

	if err := ffx.SetLogLevel(ctx, ffxutil.Warn); err != nil {
		t.Fatal(err)
	}

	netTestRealmExperimentalFlagPointer := "net.test.realm"
	if err := ffx.ConfigSet(ctx, netTestRealmExperimentalFlagPointer, "true"); err != nil {
		t.Fatalf(
			"ffx.ConfigSet(%q, true) = %s",
			netTestRealmExperimentalFlagPointer,
			err,
		)
	}

	runFfx := func(args ...string) error {
		return ffx.RunWithTarget(ctx, args...)
	}

	if err := runFfx("target", "wait"); err != nil {
		t.Fatal(err)
	}

	t.Run("starts network test realm", func(t *testing.T) {
		var buf bytes.Buffer
		ffx.SetStdoutStderr(io.MultiWriter(os.Stdout, &buf), os.Stderr)

		if err := runFfx("--machine", "json", "component", "show", NETWORK_TEST_REALM_MONIKER); err != nil {
			t.Logf(
				"ffx --machine json component show %s had error %s",
				NETWORK_TEST_REALM_MONIKER,
				err,
			)
		}

		var components []map[string]interface{}
		if err := json.Unmarshal(buf.Bytes(), &components); err != nil {
			t.Fatalf("json.Unmarshal(%q, _) = %s", buf.String(), err)
		}

		if len(components) > 0 {
			t.Fatalf(
				"ffx component show %s should return zero components, got %#v instead",
				NETWORK_TEST_REALM_MONIKER,
				components,
			)
		}

		if err := runFfx(
			"component",
			"create",
			NETWORK_TEST_REALM_MONIKER,
			NETWORK_TEST_REALM_URL,
		); err != nil {
			t.Fatalf(
				"ffx component create %s %s = %s",
				NETWORK_TEST_REALM_MONIKER,
				NETWORK_TEST_REALM_URL,
				err,
			)
		}

		if err := runFfx(
			"net-test-realm",
			NETWORK_TEST_REALM_COMPONENT_SELECTOR,
			"start-hermetic-network-realm",
			NETSTACK_VERSION,
		); err != nil {
			t.Fatalf(
				"ffx net-test-realm %s start-hermetic-network-realm %s = %s",
				NETWORK_TEST_REALM_COMPONENT_SELECTOR,
				NETSTACK_VERSION,
				err,
			)
		}
	})
}
