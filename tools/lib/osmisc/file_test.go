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

func TestFileIsOpen(t *testing.T) {
	tmpFile, err := ioutil.TempFile("", "tmpFile")
	if err != nil {
		t.Fatalf("failed to create a temporary file: %v", err)
	}
	defer os.Remove(tmpFile.Name())

	if FileIsOpen(tmpFile) == false {
		t.Errorf("file is closed when it should be open; fd: %v", tmpFile.Fd())
	}
	if err := tmpFile.Close(); err != nil {
		t.Fatalf("failed to close file: %v", err)
	}
	if FileIsOpen(tmpFile) == true {
		t.Errorf("file is open when it should be closed; fd: %v", tmpFile.Fd())
	}
}
