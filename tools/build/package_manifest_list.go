// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

import (
	"os"
	"strings"
)

// LoadPackageManifests accepts the path to a newline-delimited list of package manifests and
// returns the paths as a list of strings.
func LoadPackageManifests(packageManifestsLocation string) ([]string, error) {
	b, err := os.ReadFile(packageManifestsLocation)
	if err != nil {
		return nil, err
	}
	return strings.Split(strings.TrimSpace(string(b)), "\n"), nil
}
