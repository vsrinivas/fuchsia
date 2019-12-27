// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package osmisc

import (
	"bytes"
	"io/ioutil"
	"os"
	"path/filepath"
	"testing"
)

func TestCopyFile(t *testing.T) {
	dir, err := ioutil.TempDir("", "osmisc")
	if err != nil {
		t.Fatalf("failed to create a temporary directory: %v", err)
	}
	defer os.RemoveAll(dir)

	src := filepath.Join(dir, "src")
	in := []byte("written to src")
	if err := ioutil.WriteFile(src, in, 0444); err != nil {
		t.Fatalf("failed to write contents to src: %v", err)
	}

	dest := filepath.Join(dir, "dest")
	if err := CopyFile(src, dest); err != nil {
		t.Fatalf("failed to copy file: %v", err)
	}

	out, err := ioutil.ReadFile(dest)
	if bytes.Compare(in, out) != 0 {
		t.Fatalf("bytes read from dest not as expected: %q != %q", in, out)
	}
}
