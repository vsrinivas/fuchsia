// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

import (
	"io/ioutil"
	"path/filepath"
	"testing"
)

func TestGetBuildID(t *testing.T) {
	tempDir := t.TempDir()
	buildIDFile := filepath.Join(tempDir, "buildid")
	if err := ioutil.WriteFile(buildIDFile, []byte("abcd"), 0o600); err != nil {
		t.Fatal(err)
	}

	cases := []struct {
		name string
		bin  Binary
		err  error
	}{
		{
			name: "basic",
			bin: Binary{
				Debug:       "foo.debug",
				BuildIDFile: "buildid",
			},
			err: nil,
		},
		{
			name: "nonexistent build ID file",
			bin: Binary{
				Debug:       "foo.debug",
				BuildIDFile: "i/do/not/exist",
			},
			err: ErrBuildIDNotFound,
		},
		{
			name: "no build ID file",
			bin: Binary{
				Debug:       "foo.debug",
				BuildIDFile: "",
			},
			err: ErrBuildIDNotFound,
		},
		{
			name: "prebuilt binary with build ID in name",
			bin: Binary{
				Debug:       "../../prebuilt/foo/.build-id/ab/cd.debug",
				BuildIDFile: "",
			},
			err: nil,
		},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			id, err := c.bin.ELFBuildID(tempDir)
			if err != c.err {
				t.Errorf("Expected %v, got %v", err, c.err)
			}
			if (c.err == nil && id != "abcd") || (c.err == ErrBuildIDNotFound && id != "") {
				t.Errorf("invalid build ID found: %q", id)
			}
		})
	}
}
