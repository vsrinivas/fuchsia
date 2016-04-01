// Copyright 2016 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package file

import (
	"bytes"
	"io/ioutil"
	"math/rand"
	"os"
	"testing"
	"time"
)

const (
	numIterations = 100
	numBlocks     = 4096
	fileSize      = numBlocks * defaultBlockSize
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
	file, err := New(f)
	if err != nil {
		f.Close()
		t.Fatal("Error creating File: ", err)
	}
	defer func() {
		if err := file.Close(); err != nil {
			t.Error("Error closing file: ", err)
		}
	}()

	// Read a random number of blocks from a random offset.
	for i := int64(0); i < numIterations; i++ {
		block := r.Int63n(numBlocks)
		off := block * file.BlockSize()
		count := r.Int63n(numBlocks-block) * file.BlockSize()
		if count == 0 {
			count = file.BlockSize()
		}

		expected := buf[off : off+count]
		actual := make([]byte, count)

		if _, err := file.ReadAt(actual, off); err != nil {
			t.Errorf("Error reading %v bytes from offset %v: %v\n", count, off, err)
			continue
		}

		if !bytes.Equal(actual, expected) {
			t.Errorf("Mismatched byte slices for %v byte read from offset %v\n", count, off)
		}
	}
}

func TestWriteAt(t *testing.T) {
	name, _, r := setUp(t)
	defer os.Remove(name)

	f, err := os.OpenFile(name, os.O_RDWR, 0666)
	if err != nil {
		t.Fatal("Error opening TempFile: ", err)
	}
	file, err := New(f)
	if err != nil {
		f.Close()
		t.Fatal("Error creating File: ", err)
	}
	defer func() {
		if err := file.Close(); err != nil {
			t.Error("Error closing File: ", err)
		}
	}()

	checker, err := os.Open(name)
	if err != nil {
		t.Fatal("Error opening file: ", err)
	}
	defer checker.Close()

	// Write a random number of blocks to a random offset.
	for i := int64(0); i < numIterations; i++ {
		block := r.Int63n(numBlocks)
		off := block * file.BlockSize()
		count := r.Int63n(numBlocks-block) * file.BlockSize()
		if count == 0 {
			count = file.BlockSize()
		}

		expected := make([]byte, count)
		r.Read(expected)

		if _, err := file.WriteAt(expected, off); err != nil {
			t.Errorf("Error writing %v bytes from offset %v: %v\n", count, off, err)
			continue
		}

		actual := make([]byte, count)
		if _, err := checker.ReadAt(actual, int64(off)); err != nil {
			t.Errorf("Error reading %v bytes from checker file: %v\n", count, err)
		}
		if !bytes.Equal(actual, expected) {
			t.Errorf("Mismatched byte slices for %v byte write to offset %v\n", count, off)
		}
	}
}

func TestErrorPaths(t *testing.T) {
	name, _, _ := setUp(t)
	defer os.Remove(name)

	f, err := os.OpenFile(name, os.O_RDWR, 0666)
	if err != nil {
		t.Fatal("Error opening TempFile: ", err)
	}
	file, err := New(f)
	if err != nil {
		f.Close()
		t.Fatal("Error creating File: ", err)
	}
	defer func() {
		if err := file.Close(); err != nil {
			t.Error("Error closing File: ", err)
		}
	}()

	// Offset is not aligned.
	off := defaultBlockSize + 1
	if _, err := file.ReadAt([]byte{}, off); err == nil {
		t.Error("file.ReadAt returned a nil error for an unaligned offset")
	}
	if _, err := file.WriteAt([]byte{}, off); err == nil {
		t.Error("file.WriteAt returned a nil error for an unaligned offset")
	}

	// len(p) is not aligned.
	p := make([]byte, defaultBlockSize-1)
	off = defaultBlockSize
	if _, err := file.ReadAt(p, off); err == nil {
		t.Error("file.ReadAt returned a nil error for an unaligned len(p)")
	}
	if _, err := file.WriteAt(p, off); err == nil {
		t.Error("file.WriteAt returned a nil error for an unaligned len(p)")
	}

	// Range is out of bounds.
	off = fileSize - defaultBlockSize
	p = make([]byte, 2*defaultBlockSize)
	if _, err := file.ReadAt(p, off); err == nil {
		t.Error("file.ReadAt returned a nil error for an out of bounds range")
	}
	if _, err := file.WriteAt(p, off); err == nil {
		t.Error("file.WriteAt returned a nil error for an out of bounds range")
	}
}
