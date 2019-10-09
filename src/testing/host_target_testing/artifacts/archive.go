// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package artifacts

import (
	"fmt"
	"log"
	"os"
	"path/filepath"

	"fuchsia.googlesource.com/host_target_testing/util"
)

// Archive allows interacting with the build artifact repository.
type Archive struct {
	// lkgb (typically found in $FUCHSIA_DIR/prebuilt/tools/lkgb/lkgb) is
	// used to look up the latest build id for a given builder.
	lkgbPath string

	// artifacts (typically found in $FUCHSIA_DIR/prebuilt/tools/artifacts/artifacts)
	// is used to download artifacts for a given build id.
	artifactsPath string
}

// NewArchive creates a new Archive.
func NewArchive(lkgbPath string, artifactsPath string) *Archive {
	return &Archive{
		lkgbPath:      lkgbPath,
		artifactsPath: artifactsPath,
	}
}

// GetBuilderByName looks up a build artifact by the given name.
func (a *Archive) GetBuilder(name string) *Builder {
	return &Builder{archive: a, name: name}
}

// GetBuildByID looks up a build artifact by the given id.
func (a *Archive) GetBuildByID(id string, dir string) (*Build, error) {
	// Make sure the build exists.
	args := []string{"ls", "-build", id}
	_, stderr, err := util.RunCommand(a.artifactsPath, args...)
	if err != nil {
		if len(stderr) != 0 {
			return nil, fmt.Errorf("artifacts failed: %s: %s", err, string(stderr))
		}
		return nil, fmt.Errorf("artifacts failed: %s", err)
	}

	return &Build{ID: id, archive: a, dir: dir}, nil
}

// Download an artifact from the build id `buildID` named `src` and write it
// into a directory `dst`.
func (a *Archive) download(dir string, buildID string, src string) (string, error) {
	basename := filepath.Base(src)
	buildDir := filepath.Join(dir, buildID)
	path := filepath.Join(buildDir, basename)

	// Skip downloading if the file is already present in the build dir.
	if _, err := os.Stat(path); err == nil {
		return path, nil
	}

	log.Printf("downloading %s to %s", src, path)

	if err := os.MkdirAll(buildDir, 0755); err != nil {
		return "", err
	}

	// We don't want to leak files if we are interrupted during a download.
	// So we'll initally download into a temporary file, and only once it
	// succeeds do we rename it into the real destination.
	err := util.AtomicallyWriteFile(path, 0644, func(tmpfile *os.File) error {
		args := []string{
			"cp",
			"-build", buildID,
			"-src", src,
			"-dst", tmpfile.Name(),
		}

		_, stderr, err := util.RunCommand(a.artifactsPath, args...)
		if err != nil {
			if len(stderr) != 0 {
				return fmt.Errorf("artifacts failed: %s: %s", err, string(stderr))
			}
			return fmt.Errorf("artifacts failed: %s", err)
		}
		return nil
	})
	if err != nil {
		return "", err
	}

	return path, nil
}
