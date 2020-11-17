// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

import (
	"encoding/json"
	"fmt"
	"os"
)

type SDKArchive struct {
	// Name is the name of the SDK.
	Name string `json:"name"`

	// Label is the GN label of the associated `sdk` target. It does not include
	// the toolchain.
	Label string `json:"label"`

	// Path is the relative path to the archive within the build directory.
	Path string `json:"path"`

	// OS is the operating system which the SDK is built for. A value of "fuchsia"
	// indicates the SDK is host OS agnostic.
	OS string `json:"os"`

	// CPU is the CPU architecture which the SDK is built for.
	CPU string `json:"cpu"`
}

// loadSDKArchives reads in the entires indexed in the given manifest.
func loadSDKArchives(manifest string) ([]SDKArchive, error) {
	f, err := os.Open(manifest)
	if err != nil {
		return nil, fmt.Errorf("failed to open %s: %w", manifest, err)
	}
	defer f.Close()
	var sdkArchives []SDKArchive
	if err := json.NewDecoder(f).Decode(&sdkArchives); err != nil {
		return nil, fmt.Errorf("failed to decode %s: %w", manifest, err)
	}
	return sdkArchives, nil
}
