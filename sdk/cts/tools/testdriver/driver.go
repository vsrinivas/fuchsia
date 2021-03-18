// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package testdriver

import (
	"fmt"
	"os"
	"path/filepath"

	cipd "go.fuchsia.dev/fuchsia/sdk/cts/tools/testdriver/cipd"
)

const (
	// File permissions to use when creating the root workspace directory.
	permissions = 0755

	// TODO(jcecil): Read the CIPD package name from the CTS manifest file.
	linuxPkg = "fuchsia/sdk/gn/linux-amd64"
)

// Driver is the starting point for the Compatibility Test Suite.
type Driver struct {
	// workspacePath is the root working directory where all CTS artifacts
	// will be downloaded.
	workspacePath string

	// sdkVersion is the version of the GN SDK to download and execute the
	// CTS against.
	sdkVersion string
}

func NewDriver() *Driver {
	return &Driver{}
}

// The Driver fields are private to this testdriver package, so the following
// methods allow external tools (e.g. cmd/main.go) to configure the driver.
func (d *Driver) SetWorkspacePath(path string) { d.workspacePath = path }
func (d *Driver) SetSDKVersion(version string) { d.sdkVersion = version }

// Run executes the Compatibility Test Suite.
//
// All configuration settings should be set before calling this method.
func (d *Driver) Run() error {
	// Create workspace directory if it doesn't exist yet.
	if err := d.createWorkspace(); err != nil {
		return err
	}

	// Download the SDK archive.
	if err := d.downloadSDK(); err != nil {
		return err
	}

	return nil
}

func (d *Driver) createWorkspace() error {
	fmt.Print("Creating Workspace directory... ")
	if err := os.MkdirAll(filepath.Clean(d.workspacePath), permissions); err != nil {
		return fmt.Errorf("Failed to make directory %s: %v\n", d.workspacePath, err)
	}
	fmt.Print("Done\n")
	return nil
}

func (d *Driver) downloadSDK() error {
	fmt.Print("Downloading SDK artifact... ")

	// We separate the SDK by the version string.
	//
	// An example CTS workspace may look like this:
	//
	// <d.workspacePath>
	// ├── sdk
	// │   ├── <d.sdkVersion>
	// │   │   └── sdk content
	// │   ├── <other sdk version>
	// │   ├── ...
	//
	sdkPath := filepath.Join(d.workspacePath, "sdk", d.sdkVersion)
	sdk, err := NewSDK(linuxPkg, d.sdkVersion, sdkPath)
	if err != nil {
		return fmt.Errorf("Failed to create new SDK: %v\n", err)
	}

	client := cipd.NewCLI()
	if err := sdk.Download(client); err != nil {
		return fmt.Errorf("Failed to download SDK version %s to location %s: %v\n", sdk.Version, sdk.sdkPath, err)
	}
	fmt.Print("Done\n")
	return nil
}
