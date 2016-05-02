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
	"io/ioutil"
	"math/rand"
	"os"
	"testing"
	"time"

	"fuchsia.googlesource.com/thinfs/lib/block/blocktest"
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
