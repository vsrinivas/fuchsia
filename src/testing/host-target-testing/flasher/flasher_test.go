// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package flasher

import (
	"bytes"
	"context"
	"crypto/rand"
	"crypto/rsa"
	"os"
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
	name := filepath.Join(t.TempDir(), "ffx.sh")
	if err := os.WriteFile(name, []byte(contents), 0o700); err != nil {
		t.Fatal(err)
	}
	return name
}

func createAndRunFlasher(t *testing.T, options ...BuildFlasherOption) string {
	ffxPath := createScript(t)
	var output bytes.Buffer
	options = append(options, Stdout(&output))
	flash_manifest := "dir/flash.json"
	flasher, err := NewBuildFlasher(ffxPath, flash_manifest, false, options...)
	if err != nil {
		t.Fatal(err)
	}
	if err := flasher.Flash(context.Background()); err != nil {
		t.Fatal(err)
	}
	result := strings.ReplaceAll(output.String(), ffxPath, "ffx")
	return result
}

func TestDefault(t *testing.T) {
	result := strings.Trim(createAndRunFlasher(t), "\n")
	expected_result := "ffx target flash dir/flash.json"
	if expected_result != result {
		t.Fatalf("target flash result mismatched: " + result)
	}
}

func TestSSHKeys(t *testing.T) {
	sshKey := generatePublicKey(t)
	result := strings.Trim(createAndRunFlasher(t, SSHPublicKey(sshKey)), "\n")
	segs := strings.Fields(result)
	result = strings.Join(segs[:len(segs)-2], " ")
	expected_result := "ffx target flash --authorized-keys"
	if expected_result != result {
		t.Fatalf("target flash result mismatched: " + result)
	}
}
