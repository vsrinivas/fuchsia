// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package artifactory

import (
	"path/filepath"
	"testing"

	"github.com/google/go-cmp/cmp"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/bin/pm/build"
)

// Verifies package manifests can be properly loaded.
func TestLoadPackageManifest(t *testing.T) {
	testCases := []struct {
		name               string
		buildDirContents   map[string]string
		manifestPathToLoad string
		expectedManifest   build.PackageManifest
		wantError          bool
	}{
		{
			name: "success valid path and blobs",
			buildDirContents: map[string]string{
				"package_manifest.json": `{
					"version": "1",
					"blobs": [
						{ "merkle": "0000000000000000000000000000000000000000000000000000000000000000" }
					]
				}`,
			},
			manifestPathToLoad: "package_manifest.json",
			expectedManifest: build.PackageManifest{
				Version: "1",
				Blobs: []build.PackageBlobInfo{
					{Merkle: build.MustDecodeMerkleRoot("0000000000000000000000000000000000000000000000000000000000000000")},
				},
			},
		},
		{
			name: "failure incompatible version",
			buildDirContents: map[string]string{
				"package_manifest.json": `{
					"version": "2",
				}`,
			},
			manifestPathToLoad: "package_manifest.json",
			wantError:          true,
		},
		{
			name: "failure json improperly formatted",
			buildDirContents: map[string]string{
				"package_manifest.json": `{
					Oops. This is not valid json.
				}`,
			},
			manifestPathToLoad: "package_manifest.json",
			wantError:          true,
		}, {
			name:               "failure package manifest does not exist",
			manifestPathToLoad: "non_existent_manifest.json",
			wantError:          true,
		},
	}
	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			// Generate test env based on input.
			tempDirPath := createBuildDir(t, tc.buildDirContents)

			// Now that we're set up, we can actually load the package manifest.
			actualManifest, err := loadPackageManifest(filepath.Join(tempDirPath, tc.manifestPathToLoad))

			// Ensure the results match the expectations.
			if (err == nil) == tc.wantError {
				t.Fatalf("got error [%v], want error? %t", err, tc.wantError)
			}
			if diff := cmp.Diff(actualManifest, &tc.expectedManifest); err == nil && diff != "" {
				t.Fatalf("got manifest %#v, expected %#v", actualManifest, tc.expectedManifest)
			}
		})
	}

}
