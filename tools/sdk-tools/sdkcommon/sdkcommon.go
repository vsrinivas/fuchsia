// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package sdkcommon

import (
	"fmt"
	"os"
	"os/user"
	"path/filepath"
)

// SDKProperties holds the common data for SDK tools.
type SDKProperties struct {
	DataPath string
}

// Init initializes the SDK properties.
func (sdk *SDKProperties) Init() error {

	usr, err := user.Current()
	if err != nil {
		return err
	}
	sdk.DataPath = filepath.Join(usr.HomeDir, ".fuchsia")

	return nil
}

// GetDefaultPackageRepoDir returns the path to the package repository.
// This is the default package repository path, thinking there will
// be other repositories in the future.
func (sdk SDKProperties) GetDefaultPackageRepoDir() (string, error) {
	return filepath.Join(sdk.DataPath, "packages", "amber-files"), nil
}

// GetToolsDir returns the path to the SDK tools for the current
// CPU architecture. This is implemented by default of getting the
// directory of the currently exeecuting binary.
func (sdk SDKProperties) GetToolsDir() (string, error) {

	dir, err := filepath.Abs(filepath.Dir(os.Args[0]))
	if err != nil {
		return "", fmt.Errorf("could not get directory of currently running file: %s", err)
	}
	return dir, nil
}
