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
	"time"
)

func TestCopyFile(t *testing.T) {
	tmpdir := t.TempDir()
	src := filepath.Join(tmpdir, "src")
	in := []byte("written to src")
	if err := ioutil.WriteFile(src, in, 0o400); err != nil {
		t.Fatalf("failed to write contents to src: %v", err)
	}

	dest := filepath.Join(tmpdir, "dest")
	if err := CopyFile(src, dest); err != nil {
		t.Fatalf("failed to copy file: %v", err)
	}

	out, err := ioutil.ReadFile(dest)
	if err != nil {
		t.Fatal(err)
	}
	if bytes.Compare(in, out) != 0 {
		t.Fatalf("bytes read from dest not as expected: %q != %q", in, out)
	}
}

func TestFileIsOpen(t *testing.T) {
	f, err := os.Create(filepath.Join(t.TempDir(), "osmic-test"))
	if err != nil {
		t.Fatal(err)
	}
	if !FileIsOpen(f) {
		t.Errorf("file is closed when it should be open; fd: %v", f.Fd())
	}
	if err := f.Close(); err != nil {
		t.Fatalf("failed to close file: %v", err)
	}
	if FileIsOpen(f) {
		t.Errorf("file is open when it should be closed; fd: %v", f.Fd())
	}
}

func TestCreateFile(t *testing.T) {
	tmpdir := t.TempDir()
	path := filepath.Join(tmpdir, "subdir", "subdir2", "file")
	f, err := CreateFile(path)
	if err != nil {
		t.Fatalf("failed to create file: %v", err)
	}
	contents := []byte("contents")
	if _, err := f.Write(contents); err != nil {
		t.Fatalf("failed to write to file: %v", err)
	}
	if err := f.Close(); err != nil {
		t.Fatalf("failed to close file: %v", err)
	}
	if b, err := ioutil.ReadFile(path); err != nil {
		t.Fatalf("failed to read file: %v", err)
	} else if bytes.Compare(b, contents) != 0 {
		t.Fatalf("unexpected contents: got %s, expected %s", b, contents)
	}
}

func TestFileExists(t *testing.T) {
	tmpdir := t.TempDir()
	fA := filepath.Join(tmpdir, "fileA.txt")
	fB := filepath.Join(tmpdir, "fileB.txt")
	if err := ioutil.WriteFile(fB, []byte("content"), 0o600); err != nil {
		t.Fatalf("failed to write to %s", fB)
	}

	existsA, err := FileExists(fA)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	} else if existsA {
		t.Fatalf("%s exist when it should not", fA)
	}

	existsB, err := FileExists(fB)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	} else if !existsB {
		t.Fatalf("%s does not exist", fB)
	}
}

func TestTouch(t *testing.T) {
	testCases := []struct {
		name        string
		preexisting bool
	}{
		{
			name:        "existing file",
			preexisting: true,
		},
		{
			name:        "new file",
			preexisting: false,
		},
	}

	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			path := filepath.Join(t.TempDir(), "foo")
			if tc.preexisting {
				f, err := os.Create(path)
				if err != nil {
					t.Fatal(err)
				}
				if err := f.Close(); err != nil {
					t.Fatal(err)
				}
			}

			startTime := time.Now()
			if err := Touch(path); err != nil {
				t.Fatal(err)
			}
			endTime := time.Now()

			info, err := os.Stat(path)
			if err != nil {
				t.Fatal(err)
			}
			mtime := info.ModTime()

			if mtime.Before(startTime) {
				t.Fatalf("mtime is %s too early after Touch()", startTime.Sub(mtime))
			}
			if mtime.After(endTime) {
				t.Fatalf("mtime is %s too late after Touch()", mtime.Sub(endTime))
			}
		})
	}
}
