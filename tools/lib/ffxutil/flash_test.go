// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package ffxutil

import (
	"encoding/json"
	"os"
	"path/filepath"
	"sort"
	"testing"

	"github.com/google/go-cmp/cmp"
)

func TestGetFlashDeps(t *testing.T) {
	manifest := flashManifest{
		Manifest: manifest{
			Credentials: []string{"credentials"},
			Products: []product{
				{
					BootloaderPartitions: []partition{
						{
							Path: "image1",
						},
					},
					Name: "product1",
					Partitions: []partition{
						{
							Path: "image2",
						},
						{
							Path: "image3",
						},
					},
				},
				{
					BootloaderPartitions: []partition{
						{
							Path: "image1",
						},
					},
					Name: "product2",
					Partitions: []partition{
						{
							Path: "image4",
						},
					},
				},
			},
		},
	}
	manifestBytes, err := json.Marshal(manifest)
	if err != nil {
		t.Fatal(err)
	}
	testCases := []struct {
		name     string
		product  string
		manifest []byte
		want     []string
		wantErr  bool
	}{
		{
			name:     "deps for product1",
			product:  "product1",
			manifest: manifestBytes,
			want:     []string{flashManifestPath, "credentials", "image1", "image2", "image3"},
		},
		{
			name:     "deps for product2",
			product:  "product2",
			manifest: manifestBytes,
			want:     []string{flashManifestPath, "credentials", "image1", "image4"},
		},
		{
			name:     "invalid manifest",
			product:  "product1",
			manifest: []byte("invalid manifest"),
			want:     []string{},
			wantErr:  true,
		},
		{
			name:     "missing product",
			product:  "product3",
			manifest: manifestBytes,
			want:     []string{},
			wantErr:  true,
		},
	}
	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			sdkRoot := t.TempDir()
			if err := os.WriteFile(filepath.Join(sdkRoot, flashManifestPath), tc.manifest, os.ModePerm); err != nil {
				t.Fatalf("failed to write manifest: %s", err)
			}
			deps, err := GetFlashDeps(sdkRoot, tc.product)
			if tc.wantErr != (err != nil) {
				t.Errorf("got err: %s, want err: %v", err, tc.wantErr)
			}
			sort.Strings(deps)
			sort.Strings(tc.want)
			if diff := cmp.Diff(tc.want, deps); diff != "" {
				t.Errorf("GetFlashDeps(%s, %s) failed: (-want +got): \n%s", sdkRoot, tc.product, diff)
			}
		})
	}
}
