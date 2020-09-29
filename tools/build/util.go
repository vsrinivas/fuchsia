// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

import (
	"encoding/json"
	"fmt"
	"os"
)

// loadStringsFromJson extracts a list of strings from a json file.
func loadStringsFromJson(manifest string) ([]string, error) {
	f, err := os.Open(manifest)
	if err != nil {
		return nil, fmt.Errorf("failed to open %s: %w", manifest, err)
	}
	defer f.Close()
	var paths []string
	if err := json.NewDecoder(f).Decode(&paths); err != nil {
		return nil, fmt.Errorf("failed to decode %s: %w", manifest, err)
	}
	return paths, nil
}
