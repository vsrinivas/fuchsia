// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

import (
	"io"
	"io/ioutil"
	"os"
	"testing"
)

func TestGetBuildID(t *testing.T) {
	tempDir, err := ioutil.TempDir("", "")
	if err != nil {
		t.Fatal(err)
	}
	defer os.RemoveAll(tempDir)

	buildIDFile, err := fileWithContent(tempDir, "abcd")
	if err != nil {
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
				BuildIDFile: buildIDFile,
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
			if err == c.err {
				if (c.err == nil && id != "abcd") || (c.err == ErrBuildIDNotFound && id != "") {
					t.Errorf("invalid build ID found: %q", id)
				}
			}
		})
	}
}

func fileWithContent(dir, content string) (string, error) {
	f, err := ioutil.TempFile(dir, "")
	if err != nil {
		return "", err
	}
	defer f.Close()
	if _, err := io.WriteString(f, content); err != nil {
		return "", err
	}
	return f.Name(), nil
}
