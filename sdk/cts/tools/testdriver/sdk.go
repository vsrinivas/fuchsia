// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testdriver

import (
	"fmt"
	"log"
	"os"
	"path/filepath"

	cipd "go.fuchsia.dev/fuchsia/sdk/cts/tools/testdriver/cipd"
)

// SDK is used to download and manipulate the GN SDK.
type SDK struct {
	// Package: CIPD package name, used to retrieve the package from CIPD.
	//
	// Format: path/to/cipd/package/name/<os>-<arch>
	// Examples:
	//   fuchsia/sdk/gn/linux-amd64
	//   fuchsia/sdk/gn/mac-amd64
	//
	// Required
	Package string `json:"cipd_pkg"`

	// Version: SDK version string, used to retrieve the package from CIPD.
	//
	// Format: #.YYYYMMDD.#.#
	// Examples:
	//   3.20210311.3.1
	//
	// Required
	Version string `json:"version"`

	// sdkPath: Location to download and extract the SDK artifact.
	//
	// Required
	sdkPath string
}

// NewSDK returns an SDK instance that can be used to download the SDK archive,
// and run the tools and scripts that it contains.
func NewSDK(pkg, version, path string) (*SDK, error) {
	return &SDK{
		Package: pkg,
		Version: version,
		sdkPath: filepath.Join(path, "sdk"),
	}, nil
}

// Download retrieves the Fuchsia SDK version specified by sdk.Version,
// and extracts it to the location specified by sdk.sdkPath.
func (sdk *SDK) Download(c cipd.CIPD) error {
	log.Printf("Downloading SDK version %v...\n", sdk.Version)

	// Ensure the target directory exists for the sdk directory.
	if err := os.MkdirAll(sdk.sdkPath, 0700); err != nil {
		return fmt.Errorf("Failed to create SDK directory %s: %v", sdk.sdkPath, err)
	}

	// Find the CIPD version string that matches this SDK package name and version string.
	tags := []*cipd.Tag{
		cipd.NewTag("version", sdk.Version),
	}
	refs := []*cipd.Ref{}
	pkg, err := c.GetVersion(sdk.Package, tags, refs)
	if err != nil {
		return fmt.Errorf("Failed to retrieve the CIPD version for this SDK. Package: %s, Version: %s, Error: %v\n", sdk.Package, sdk.Version, err)
	}

	// Download the SDK to sdk.sdkPath.
	err = c.Download(pkg, sdk.sdkPath)
	if err != nil {
		return fmt.Errorf("Failed to download the SDK. Package: %s, Version: %s, Error: %v\n", sdk.Package, sdk.Version, err)
	}
	return nil
}
