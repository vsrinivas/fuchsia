// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package file

import (
	"bytes"
	"io/ioutil"
	"math/rand"
	"os"
	"testing"
	"time"

	"fuchsia.googlesource.com/thinfs/block/blocktest"
)

const (
	numBlocks = 4096

	defaultBlockSize int64 = 1024
	fileSize               = numBlocks * defaultBlockSize
)

func setUp(t *testing.T) (string, []byte, *rand.Rand) {
	seed := time.Now().UTC().UnixNano()
	t.Log("Seed is", seed)
	r := rand.New(rand.NewSource(seed))

	buf := make([]byte, fileSize)
	r.Read(buf)

	tmpFile, err := ioutil.TempFile("", "file_test")
	if err != nil {
		t.Fatal("Error creating temp file: ", err)
	}

	name := tmpFile.Name()

	if _, err := tmpFile.Write(buf); err != nil {
		t.Fatal("Error writing random data to temp file: ", err)
	}

	if err := tmpFile.Close(); err != nil {
		t.Fatal("Error closing temp file: ", err)
	}

	return name, buf, r
}

func TestReadAt(t *testing.T) {
	name, buf, r := setUp(t)
	defer os.Remove(name)

	f, err := os.OpenFile(name, os.O_RDWR, 0666)
	if err != nil {
		t.Fatal("Error opening TempFile: ", err)
	}
	file, err := New(f, defaultBlockSize)
	if err != nil {
		f.Close()
		t.Fatal("Error creating File: ", err)
	}
	defer func() {
		if err := file.Close(); err != nil {
			t.Error("Error closing file: ", err)
		}
	}()

	blocktest.ReadAt(t, file, r, buf)
}

func TestWriteAt(t *testing.T) {
	name, buf, r := setUp(t)
	defer os.Remove(name)

	f, err := os.OpenFile(name, os.O_RDWR, 0666)
	if err != nil {
		t.Fatal("Error opening TempFile: ", err)
	}
	file, err := New(f, defaultBlockSize)
	if err != nil {
		f.Close()
		t.Fatal("Error creating File: ", err)
	}
	defer func() {
		if err := file.Close(); err != nil {
			t.Error("Error closing file: ", err)
		}
	}()

	blocktest.WriteAt(t, file, r, buf)
}

func TestErrorPaths(t *testing.T) {
	name, _, _ := setUp(t)
	defer os.Remove(name)

	f, err := os.OpenFile(name, os.O_RDWR, 0666)
	if err != nil {
		t.Fatal("Error opening TempFile: ", err)
	}
	file, err := New(f, defaultBlockSize)
	if err != nil {
		f.Close()
		t.Fatal("Error creating File: ", err)
	}
	defer func() {
		if err := file.Close(); err != nil {
			t.Error("Error closing File: ", err)
		}
	}()

	blocktest.ErrorPaths(t, file)
}

func TestRangeReadWrite(t *testing.T) {
	name, _, _ := setUp(t)
	defer os.Remove(name)

	f, err := os.OpenFile(name, os.O_RDWR, 0666)
	if err != nil {
		t.Fatal(err)
	}

	file, err := NewRange(f, 512, 1024, 10240)
	if err != nil {
		f.Close()
		t.Fatal(err)
	}
	defer func() {
		if err := file.Close(); err != nil {
			t.Error("Error closing File: ", err)
		}
	}()

	want := make([]byte, 512)
	copy(want, []byte("hello world"))
	if _, err := file.WriteAt(want, 0); err != nil {
		t.Fatal(err)
	}

	b := make([]byte, len(want))
	if _, err := f.ReadAt(b, 1024); err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(b, want) {
		t.Errorf("Range WriteAt: got %x, want %x", b, want)
	}

	b = make([]byte, len(want))
	if _, err := file.ReadAt(b, 0); err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(b, want) {
		t.Errorf("Range ReadAt: got %x, want %x", b, want)
	}

	// TODO(raggi): I'm not aware of a portable / good way to test discard here.
}
