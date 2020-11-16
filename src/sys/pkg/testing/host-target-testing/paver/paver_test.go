// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package paver

import (
	"bytes"
	"context"
	"crypto/rand"
	"crypto/rsa"
	"fmt"
	"golang.org/x/crypto/ssh"
	"io/ioutil"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// The easiest way to make a fake key is to just generate a real one.
func GeneratePublicKey() (ssh.PublicKey, error) {
	privateKey, err := rsa.GenerateKey(rand.Reader, 2048)
	if err != nil {
		return nil, err
	}
	return ssh.NewPublicKey(&privateKey.PublicKey)
}

func CreateScript(fileName string) (script string, err error) {
	file, err := ioutil.TempFile("", fileName)
	if err != nil {
		return "", err
	}
	defer file.Close()

	// Script outputs its name and all its arguments.
	contents := `#!/bin/sh
echo "$0 $@"
`

	if _, err := file.Write([]byte(contents)); err != nil {
		os.Remove(file.Name())
		return "", err
	}

	if err := file.Chmod(0744); err != nil {
		os.Remove(file.Name())
		return "", err
	}

	return file.Name(), nil
}

func CreateAndRunPaver(options ...BuildPaverOption) (zedbootPaverArgs []string, paverArgs []string, err error) {
	bootserverPath, err := CreateScript("bootserver_tool")
	if err != nil {
		return nil, nil, err
	}
	defer os.Remove(bootserverPath)

	var output bytes.Buffer
	options = append(options, Stdout(&output))
	paver, err := NewBuildPaver(bootserverPath, filepath.Dir(bootserverPath), options...)
	if err != nil {
		return nil, nil, err
	}

	if err := paver.Pave(context.Background(), "a-fake-device-name"); err != nil {
		return nil, nil, err
	}

	outputs := strings.Split(output.String(), "\n")
	zedbootPaverArgs = strings.Split(outputs[0], " ")
	paverArgs = strings.Split(outputs[1], " ")

	if zedbootPaverArgs[0] != bootserverPath || zedbootPaverArgs[4] != "pave-zedboot" {
		err := fmt.Errorf("Paver called the wrong bootserver or mode. Expected %s with mode pave-zedboot, actual %s with mode %s",
			bootserverPath, zedbootPaverArgs[0], zedbootPaverArgs[4])
		return nil, nil, err
	}

	if paverArgs[0] != bootserverPath || paverArgs[4] != "pave" {
		err := fmt.Errorf("Paver called the wrong bootserver or mode. Expected %s with mode pave, actual %s with mode %s",
			bootserverPath, paverArgs[0], paverArgs[4])
		return nil, nil, err
	}

	return
}

func TestDefault(t *testing.T) {
	zedbootPaverArgs, paverArgs, err := CreateAndRunPaver()
	if err != nil {
		t.Fatal(err)
	}

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
	sshKey, err := GeneratePublicKey()
	if err != nil {
		t.Fatal(err)
	}

	_, paverArgs, err := CreateAndRunPaver(SSHPublicKey(sshKey))
	if err != nil {
		t.Fatal(err)
	}

	hasAuthorizedKeys := false
	for i, arg := range paverArgs {
		if arg == "--authorized-keys" {
			// Check that there's at least one more argument.
			if i+1 < len(paverArgs) {
				hasAuthorizedKeys = true
			}
		}
	}

	if !hasAuthorizedKeys {
		t.Fatalf("Missing authorized keys file in paver arguments.")
	}
}

func TestOverrideSlotA(t *testing.T) {
	zirconAFile, err := ioutil.TempFile("", "zircona.*")
	if err != nil {
		t.Fatal(err)
	}
	defer func() {
		zirconAFile.Close()
		os.Remove(zirconAFile.Name())
	}()

	_, paverArgs, err := CreateAndRunPaver(OverrideSlotA(zirconAFile.Name()))
	if err != nil {
		t.Fatal(err)
	}

	var zirconAPath string
	for i, arg := range paverArgs {
		if arg == "--zircona" {
			if i+1 < len(paverArgs) {
				zirconAPath = paverArgs[i+1]
			}
		}
	}

	if zirconAPath != zirconAFile.Name() {
		t.Fatalf("Missing zircon A image in paver arguments.")
	}
}

func TestOverrideVBMetaA(t *testing.T) {
	vbMetaAFile, err := ioutil.TempFile("", "vbmetaa.*")
	if err != nil {
		t.Fatal(err)
	}
	defer func() {
		vbMetaAFile.Close()
		os.Remove(vbMetaAFile.Name())
	}()

	_, paverArgs, err := CreateAndRunPaver(
		OverrideVBMetaA(vbMetaAFile.Name()))
	if err != nil {
		t.Fatal(err)
	}

	var vbmetaAPath string
	for i, arg := range paverArgs {
		if arg == "--vbmetaa" {
			if i+1 < len(paverArgs) {
				vbmetaAPath = paverArgs[i+1]
			}
		}
	}

	if vbmetaAPath != vbMetaAFile.Name() {
		t.Fatalf("Missing vbmeta A image in paver arguments.")
	}
}
