// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package paver

import (
	"io/ioutil"
	"log"
	"os"
	"os/exec"

	"golang.org/x/crypto/ssh"
)

type Paver struct {
	script    string
	publicKey ssh.PublicKey
}

// NewPaver constructs a new paver, where script is the script to the "pave.sh"
// script, and `publicKey` is the public key baked into the device as an
// authorized key.
func NewPaver(script string, publicKey ssh.PublicKey) *Paver {
	return &Paver{script: script, publicKey: publicKey}
}

// Pave runs a paver service for one pave. If `deviceName` is not empty, the
// pave will only be applied to the specified device.
func (p *Paver) Pave(deviceName string) error {
	log.Printf("paving device %q", deviceName)
	path, err := exec.LookPath(p.script)
	if err != nil {
		return err
	}

	// Write out the public key's authorized keys.
	authorizedKeys, err := ioutil.TempFile("", "")
	if err != nil {
		return err
	}
	defer os.Remove(authorizedKeys.Name())

	if _, err := authorizedKeys.Write(ssh.MarshalAuthorizedKey(p.publicKey)); err != nil {
		return err
	}

	if err := authorizedKeys.Close(); err != nil {
		return err
	}

	args := []string{"-1", "--authorized-keys", authorizedKeys.Name()}
	if deviceName != "" {
		args = append(args, "-n", deviceName)
	}
	log.Printf("running: %s %q", path, args)
	cmd := exec.Command(path, args...)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	return cmd.Run()
}
