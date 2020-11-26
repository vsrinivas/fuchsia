// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package paver

import (
	"bytes"
	"context"
	"crypto/rand"
	"crypto/rsa"
	"io/ioutil"
	"path/filepath"
	"strings"
	"testing"

	"golang.org/x/crypto/ssh"
)

// The easiest way to make a fake key is to just generate a real one.
func generatePublicKey(t *testing.T) ssh.PublicKey {
	privateKey, err := rsa.GenerateKey(rand.Reader, 2048)
	if err != nil {
		t.Fatal(err)
	}
	pub, err := ssh.NewPublicKey(&privateKey.PublicKey)
	if err != nil {
		t.Fatal(err)
	}
	return pub
}

// createScript returns the path to a bash script that outputs its name and
// all its arguments.
func createScript(t *testing.T) string {
	contents := "#!/bin/sh\necho \"$0 $@\"\n"
	name := filepath.Join(t.TempDir(), "bootserver.sh")
	if err := ioutil.WriteFile(name, []byte(contents), 0o700); err != nil {
		t.Fatal(err)
	}
	return name
}

func createAndRunPaver(t *testing.T, options ...BuildPaverOption) (zedbootPaverArgs []string, paverArgs []string) {
	bootserverPath := createScript(t)
	var output bytes.Buffer
	options = append(options, Stdout(&output))
	paver, err := NewBuildPaver(bootserverPath, filepath.Dir(bootserverPath), options...)
	if err != nil {
		t.Fatal(err)
	}
	if err := paver.Pave(context.Background(), "a-fake-device-name"); err != nil {
		t.Fatal(err)
	}

	outputs := strings.Split(output.String(), "\n")
	zedbootPaverArgs = strings.Split(outputs[0], " ")
	paverArgs = strings.Split(outputs[1], " ")

	if zedbootPaverArgs[0] != bootserverPath || zedbootPaverArgs[4] != "pave-zedboot" {
		t.Fatalf("Paver called the wrong bootserver or mode. Expected %s with mode pave-zedboot, actual %s with mode %s",
			bootserverPath, zedbootPaverArgs[0], zedbootPaverArgs[4])
	}
	if paverArgs[0] != bootserverPath || paverArgs[4] != "pave" {
		t.Fatalf("Paver called the wrong bootserver or mode. Expected %s with mode pave, actual %s with mode %s",
			bootserverPath, paverArgs[0], paverArgs[4])
	}
	return
}

func TestDefault(t *testing.T) {
	zedbootPaverArgs, paverArgs := createAndRunPaver(t)

	{
		var deviceName string
		hasAllowVersionMismatch := false
		for i, arg := range zedbootPaverArgs {
			if arg == "-n" {
				if i+1 < len(zedbootPaverArgs) {
					deviceName = zedbootPaverArgs[i+1]
				}
			} else if arg == "--allow-zedboot-version-mismatch" {
				hasAllowVersionMismatch = true
			}
		}

		if deviceName != "a-fake-device-name" {
			t.Fatalf("Missing device name in zedboot paver arguments.")
		}
		if !hasAllowVersionMismatch {
			t.Fatalf("Missing allow version mismatch flag in zedboot paver arguments.")
		}
	}

	{
		var deviceName string
		hasFailFastIfVersionMismatch := false
		for i, arg := range paverArgs {
			if arg == "-n" {
				if i+1 < len(paverArgs) {
					deviceName = paverArgs[i+1]
				}
			} else if arg == "--fail-fast-if-version-mismatch" {
				hasFailFastIfVersionMismatch = true
			}
		}

		if deviceName != "a-fake-device-name" {
			t.Fatalf("Missing device name in paver arguments.")
		}
		if !hasFailFastIfVersionMismatch {
			t.Fatalf("Missing allow version mismatch flag in paver arguments.")
		}
	}
}

func TestSSHKeys(t *testing.T) {
	sshKey := generatePublicKey(t)
	_, paverArgs := createAndRunPaver(t, SSHPublicKey(sshKey))
	hasAuthorizedKeys := false
	for i, arg := range paverArgs {
		if arg == "--authorized-keys" {
			// Check that there's at least one more argument.
			if i+1 < len(paverArgs) {
				hasAuthorizedKeys = true
				break
			}
		}
	}
	if !hasAuthorizedKeys {
		t.Fatalf("Missing authorized keys file in paver arguments.")
	}
}

func TestOverrideSlotA(t *testing.T) {
	name := t.TempDir()
	_, paverArgs := createAndRunPaver(t, OverrideSlotA(name))
	var zirconAPath string
	for i, arg := range paverArgs {
		if arg == "--zircona" {
			if i+1 < len(paverArgs) {
				zirconAPath = paverArgs[i+1]
			}
		}
	}
	if zirconAPath != name {
		t.Fatalf("Missing zircon A image in paver arguments.")
	}
}

func TestOverrideVBMetaA(t *testing.T) {
	name := t.TempDir()
	_, paverArgs := createAndRunPaver(t, OverrideVBMetaA(name))
	var vbmetaAPath string
	for i, arg := range paverArgs {
		if arg == "--vbmetaa" {
			if i+1 < len(paverArgs) {
				vbmetaAPath = paverArgs[i+1]
			}
		}
	}
	if vbmetaAPath != name {
		t.Fatalf("Missing vbmeta A image in paver arguments.")
	}
}
