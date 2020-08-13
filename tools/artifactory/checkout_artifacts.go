// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package artifactory

import (
	"fmt"
	"path"
	"path/filepath"

	"go.fuchsia.dev/fuchsia/tools/build"
)

const (
	jiriSnapshotName     = "jiri_snapshot"
	jiriSnapshotDestName = "jiri_snapshot.xml"
)

// JiriSnapshotUpload returns an upload object representing a jiri snapshot.
func JiriSnapshotUpload(mods *build.Modules, namespace string) (*Upload, error) {
	for _, artifact := range mods.CheckoutArtifacts() {
		if artifact.Name == jiriSnapshotName {
			absPath, err := filepath.Abs(filepath.Join(mods.BuildDir(), artifact.Path))
			if err != nil {
				return nil, fmt.Errorf("could not determine jiri snapshot path: %w", err)
			}
			return &Upload{
				Source:      absPath,
				Destination: path.Join(namespace, jiriSnapshotDestName),
			}, nil
		}
	}
	return nil, fmt.Errorf("failed to find a jiri snapsot")
}
