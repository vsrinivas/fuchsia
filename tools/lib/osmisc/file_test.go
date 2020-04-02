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

func TestCreateFile(t *testing.T) {
	dir, err := ioutil.TempDir("", "tmpdir")
	if err != nil {
		t.Fatalf("failed to create a temporary directory: %v", err)
	}
	defer os.RemoveAll(dir)

	path := filepath.Join(dir, "subdir", "subdir2", "file")
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
	tmpdir, err := ioutil.TempDir("", "")
	if err != nil {
		t.Fatalf("failed to create temp dir: %v", err)
	}
	defer os.RemoveAll(tmpdir)

	fA := filepath.Join(tmpdir, "fileA.txt")
	fB := filepath.Join(tmpdir, "fileB.txt")
	if err := ioutil.WriteFile(fB, []byte("content"), os.ModePerm); err != nil {
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

func TestDirIsEmpty(t *testing.T) {
	tmpdir, err := ioutil.TempDir("", "")
	if err != nil {
		t.Fatalf("failed to create temp dir: %v", err)
	}
	defer os.RemoveAll(tmpdir)

	// Directory should start off empty.
	empty, err := DirIsEmpty(tmpdir)
	if err != nil {
		t.Fatal(err.Error())
	} else if !empty {
		t.Fatalf("directory should be empty")
	}

	if err := ioutil.WriteFile(filepath.Join(tmpdir, "file.txt"), []byte("content"), os.ModePerm); err != nil {
		t.Fatal(err.Error())
	}

	// Directory should now be non-empty.
	empty, err = DirIsEmpty(tmpdir)
	if err != nil {
		t.Fatal(err.Error())
	} else if empty {
		t.Fatalf("directory should be non-empty")
	}

	// Non-existent directories should be empty by convention.
	nonexistentSubdir := filepath.Join(tmpdir, "i_dont_exist")
	empty, err = DirIsEmpty(nonexistentSubdir)
	if err != nil {
		t.Fatal(err.Error())
	} else if !empty {
		t.Fatalf("non-existent directory should be empty")
	}

}
