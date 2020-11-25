// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package artifactory

import (
	"io/ioutil"
	"os"
	"path/filepath"
	"testing"

	"github.com/google/go-cmp/cmp"
	"go.fuchsia.dev/fuchsia/src/sys/pkg/bin/pm/build"
)

// createBuildDir generates a mock build directory for the tests to use.
//
// The tree will be cleaned up automatically.
func createBuildDir(t *testing.T, files map[string]string) string {
	dir := t.TempDir()
	for filename, data := range files {
		if err := os.MkdirAll(filepath.Join(dir, filepath.Dir(filename)), 0o700); err != nil {
			t.Fatal(err)
		}
		if err := ioutil.WriteFile(filepath.Join(dir, filename), []byte(data), 0o600); err != nil {
			t.Fatal(err)
		}
	}
	return dir
}

// Implements pkgManifestModules
type mockPkgManifestModules struct {
	pkgManifests []string
	buildDir     string
}

func (m mockPkgManifestModules) BuildDir() string {
	return m.buildDir
}

func (m mockPkgManifestModules) PackageManifests() []string {
	return m.pkgManifests
}

// Verifies we can parse the build dir to produce a blobs Upload.
func TestBlobsUpload(t *testing.T) {
	testCases := []struct {
		name              string
		buildDirContents  map[string]string
		packageManifests  []string
		uploadDestination string
		expectedUpload    Upload
		wantError         bool
	}{
		{
			name: "success",
			buildDirContents: map[string]string{
				"a": `{
					"version": "1",
					"blobs": [
						{
            				"merkle": "0000000000000000000000000000000000000000000000000000000000000000"
						},
						{
            				"merkle": "1111111111111111111111111111111111111111111111111111111111111111"
						}
					]
				}`,
				"b": `{
					"version": "1",
					"blobs": [
						{
            				"merkle": "1111111111111111111111111111111111111111111111111111111111111111"
						}
					]
				}`,
			},
			packageManifests:  []string{"a", "b", "c"},
			uploadDestination: "namespace/all_blobs.json",
			expectedUpload: Upload{
				Compress:    true,
				Destination: "namespace/all_blobs.json",
				Contents: []byte("[" +
					"{" +
					"\"source_path\":\"\"," +
					"\"path\":\"\"," +
					"\"merkle\":\"0000000000000000000000000000000000000000000000000000000000000000\"," +
					"\"size\":0" +
					"}," +
					"{" +
					"\"source_path\":\"\"," +
					"\"path\":\"\"," +
					"\"merkle\":\"1111111111111111111111111111111111111111111111111111111111111111\"," +
					"\"size\":0" +
					"}" +
					"]"),
			},
		},
		{
			name: "failure package manifest improper json formatting",
			buildDirContents: map[string]string{
				"a": `{
					Oops. This is not valid json.
				}`,
			},
			packageManifests: []string{"a"},
			wantError:        true,
		},
	}
	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			// Generate test env based on input.
			tempDirPath := createBuildDir(t, tc.buildDirContents)

			// Now that we're set up, we can actually determine the blobs Upload.
			mockModules := mockPkgManifestModules{
				pkgManifests: tc.packageManifests,
				buildDir:     tempDirPath,
			}
			actualUpload, err := blobsUpload(mockModules, tc.uploadDestination)

			// Ensure the results match the expectations.
			if (err == nil) == tc.wantError {
				t.Fatalf("got error [%v], want error? %t", err, tc.wantError)
			}
			if diff := cmp.Diff(actualUpload, tc.expectedUpload); diff != "" {
				t.Fatalf("got upload %#v, expected %#v", actualUpload, tc.expectedUpload)
			}
		})
	}
}

// Verifies blobs can be properly loaded via the package manifests.
func TestLoadBlobsFromPackageManifests(t *testing.T) {
	testCases := []struct {
		name             string
		buildDirContents map[string]string
		packageManifests []string
		expectedBlobs    []build.PackageBlobInfo
		wantError        bool
	}{
		{
			name: "success one package manifest",
			buildDirContents: map[string]string{
				"a": `{
					"version": "1",
					"blobs": [
						{
            				"merkle": "1111111111111111111111111111111111111111111111111111111111111111"
						}
					]
				}`,
			},
			packageManifests: []string{"a"},
			expectedBlobs: []build.PackageBlobInfo{
				{Merkle: build.MustDecodeMerkleRoot("1111111111111111111111111111111111111111111111111111111111111111")},
			},
		},
		{
			name: "success multiple nested package manifests",
			buildDirContents: map[string]string{
				"a/b": `{
					"version": "1",
					"blobs": [
						{ "merkle": "0000000000000000000000000000000000000000000000000000000000000000" },
						{ "merkle": "1111111111111111111111111111111111111111111111111111111111111111" }
					]
				}`,
				"a/a/b": `{
					"version": "1",
					"blobs": [
						{ "merkle": "2222222222222222222222222222222222222222222222222222222222222222" }
					]
				}`,
				"c/d": `{
					"version": "1",
					"blobs": [
						{ "merkle": "3333333333333333333333333333333333333333333333333333333333333333" }
					]
				}`,
			},
			packageManifests: []string{"a/b", "a/a/b", "c/d"},
			expectedBlobs: []build.PackageBlobInfo{
				{Merkle: build.MustDecodeMerkleRoot("0000000000000000000000000000000000000000000000000000000000000000")},
				{Merkle: build.MustDecodeMerkleRoot("1111111111111111111111111111111111111111111111111111111111111111")},
				{Merkle: build.MustDecodeMerkleRoot("2222222222222222222222222222222222222222222222222222222222222222")},
				{Merkle: build.MustDecodeMerkleRoot("3333333333333333333333333333333333333333333333333333333333333333")},
			},
		},
		{
			name: "success multiple package manifests,  duplicate blobs",
			buildDirContents: map[string]string{
				"a": `{
					"version": "1",
					"blobs": [
						{ "merkle": "0000000000000000000000000000000000000000000000000000000000000000" },
						{ "merkle": "1111111111111111111111111111111111111111111111111111111111111111" },
						{ "merkle": "1111111111111111111111111111111111111111111111111111111111111111" }
					]
				}`,
				"b": `{
					"version": "1",
					"blobs": [
						{ "merkle": "0000000000000000000000000000000000000000000000000000000000000000" }
					]
				}`,
			},
			packageManifests: []string{"a", "b"},
			expectedBlobs: []build.PackageBlobInfo{
				{Merkle: build.MustDecodeMerkleRoot("0000000000000000000000000000000000000000000000000000000000000000")},
				{Merkle: build.MustDecodeMerkleRoot("1111111111111111111111111111111111111111111111111111111111111111")},
			},
		},
		{
			name:             "succeess package manifest does not exist",
			packageManifests: []string{"non-existent-manifest"},
		},
		{
			name: "failure package manifest improper json formatting",
			buildDirContents: map[string]string{
				"a": `{
					"version": "1",
					"blobs": [
						Oops. This is not valid json.
					]
				}`,
			},
			packageManifests: []string{"a"},
			wantError:        true,
		},
	}

	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			// Generate test env based on input.
			tempDirPath := createBuildDir(t, tc.buildDirContents)
			paths := make([]string, len(tc.packageManifests))
			for _, path := range tc.packageManifests {
				paths = append(paths, filepath.Join(tempDirPath, path))
			}

			// Now that we're set up, we can actually load the blobs.
			actualBlobs, err := loadBlobsFromPackageManifests(paths)

			// Ensure the results match the expectations.
			if (err == nil) == tc.wantError {
				t.Fatalf("got error [%v], want error? %t", err, tc.wantError)
			}
			if diff := cmp.Diff(actualBlobs, tc.expectedBlobs); diff != "" {
				t.Fatalf("got blobs %#v, expected %#v", actualBlobs, tc.expectedBlobs)
			}
		})
	}
}
