// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package util

import (
	"fmt"
	"os"
	"path/filepath"

	"go.fuchsia.dev/fuchsia/tools/lib/osmisc"
)

func GetHostOutDirectory() (string, error) {
	executablePath, err := os.Executable()
	if err != nil {
		return "", err
	}
	dir, err := filepath.Abs(filepath.Dir(executablePath))
	if err != nil {
		return "", err
	}
	parent, filePart := filepath.Split(dir)
	if filePart == "host-tools" {
		// We're running at desk and actually need the host_x64 directory instead.
		return filepath.Join(parent, "host_x64"), nil
	}
	return dir, nil
}

func DutSshKeyPath() (string, error) {
	hostOutDir, err := GetHostOutDirectory()
	if err != nil {
		return "", err
	}
	// TODO(https://fxbug.dev/112513): rm in_tree path component once
	// soft-transition has occurred.
	keyFilepath := filepath.Join(hostOutDir, "ssh_keys", "in_tree", "dut_ssh_key")
	fileExists, err := osmisc.FileExists(keyFilepath)
	if err != nil {
		return "", fmt.Errorf("osmisc.FileExists(%q) = %w", keyFilepath, err)
	}
	if !fileExists {
		return "", fmt.Errorf("No SSH key file found at %s", keyFilepath)
	}
	return keyFilepath, nil
}

func DutAuthorizedKeysPath() (string, error) {
	hostOutDir, err := GetHostOutDirectory()
	if err != nil {
		return "", err
	}
	// TODO(https://fxbug.dev/112513): rm in_tree path component once
	// soft-transition has occurred.
	authorizedKeysFilepath := filepath.Join(hostOutDir, "ssh_keys", "in_tree", "dut_authorized_keys")
	fileExists, err := osmisc.FileExists(authorizedKeysFilepath)
	if err != nil {
		return "", fmt.Errorf("osmisc.FileExists(%q) = %w", authorizedKeysFilepath, err)
	}
	if !fileExists {
		return "", fmt.Errorf("No authorized_keys file found at %s", authorizedKeysFilepath)
	}
	return authorizedKeysFilepath, nil
}
