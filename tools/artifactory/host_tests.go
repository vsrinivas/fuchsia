// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package artifactory

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
	"os"
	"path"
	"path/filepath"

	"go.fuchsia.dev/fuchsia/tools/build"
)

// HostTestUploads returns a set of Uploads corresponding to the host tests in testSpecs.
func HostTestUploads(testSpecs []build.TestSpec, buildDir, namespace string) ([]Upload, error) {
	var uploads []Upload
	// Ensure we don't try to upload the same file twice, which can happen due to a file
	// appearing in multiple runtime deps lists, or as a test path and a runtime dep.
	sourceSet := make(map[string]bool)
	for _, ts := range testSpecs {
		test := &ts.Test
		if test.OS != "linux" && test.OS != "mac" {
			continue
		}
		if _, seen := sourceSet[test.Path]; !seen {
			uploads = append(uploads, Upload{
				Source:      filepath.Join(buildDir, test.Path),
				Destination: path.Join(namespace, test.Path),
			})
			sourceSet[test.Path] = true
		}
		if test.RuntimeDepsFile == "" {
			continue
		}
		depsPath := filepath.Join(buildDir, test.RuntimeDepsFile)
		depsBytes, err := ioutil.ReadFile(depsPath)
		if err != nil {
			return nil, err
		}
		var deps []string
		err = json.Unmarshal(depsBytes, &deps)
		if err != nil {
			return nil, fmt.Errorf("failed to read runtime deps for test %s: %w", test.Name, err)
		}
		// regular = not a directory.
		var regularDeps []string
		for _, dep := range deps {
			depPath := filepath.Join(buildDir, dep)
			depInfo, err := os.Stat(depPath)
			if err != nil {
				return nil, fmt.Errorf("failed to stat runtime dep file: %w", err)
			}
			if depInfo.IsDir() {
				err = filepath.Walk(
					depPath,
					func(path string, info os.FileInfo, err error) error {
						if err != nil {
							return err
						}
						if info.Mode().IsRegular() {
							relPath, err := filepath.Rel(buildDir, path)
							if err != nil {
								return err
							}
							regularDeps = append(regularDeps, relPath)
						}
						return nil
					})
				if err != nil {
					return nil, fmt.Errorf("error during Walk(%s): %w", depPath, err)
				}
			} else {
				regularDeps = append(regularDeps, dep)
			}
		}
		for _, dep := range regularDeps {
			if _, seen := sourceSet[dep]; !seen {
				uploads = append(uploads, Upload{
					Source: filepath.Join(buildDir, dep),
					// Technically we should split dep into its path components using filepath
					// and re-join it using path, but this will only be an issue on Windows.
					// Since above we filter out any test that's not for linux or mac, this
					// is not an issue.
					Destination: path.Join(namespace, dep),
				})
				sourceSet[dep] = true
			}
		}
	}
	return uploads, nil
}
