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

// Package testutil provides utilities which help test the FAT filesystem.
package testutil

import (
	"os"
	"os/exec"
	"runtime"
	"strconv"
	"strings"
	"testing"

	"fuchsia.googlesource.com/thinfs/lib/block"
	"fuchsia.googlesource.com/thinfs/lib/block/file"
	"fuchsia.googlesource.com/thinfs/lib/thinio"
)

// FileFAT describes a file-backed FAT image which can be treated like a FAT filesystem.
type FileFAT struct {
	t    *testing.T
	name string
}

// MkfsFAT creates a new FAT filesystem image.
// The name of the FAT image is returned.
func MkfsFAT(t *testing.T, size string, numFATs, numHiddenSectors, sectorsPerCluster, sectorSize int) *FileFAT {
	// OS X has some difficulties with upper-case arguments to dd.
	if runtime.GOOS == "darwin" {
		size = strings.ToLower(size)
	}

	name := "testfs"
	seek := "seek=" + size
	outputArg := "of=" + name
	cmd := exec.Command("dd", "if=/dev/zero", outputArg, "bs=1", "count=0", seek)
	if err := cmd.Run(); err != nil {
		t.Fatal(err)
	}

	numFATsArg := "-f " + strconv.Itoa(numFATs)
	numHiddenArg := "-h " + strconv.Itoa(numHiddenSectors)
	sectorsPerClusterArg := "-s " + strconv.Itoa(sectorsPerCluster)
	sectorSizeArg := "-S " + strconv.Itoa(sectorSize)
	cmd = exec.Command("mkfs.fat", numFATsArg, numHiddenArg, sectorsPerClusterArg, sectorSizeArg, name)
	if err := cmd.Run(); err != nil {
		t.Fatal(err)
	}

	fat := &FileFAT{
		t:    t,
		name: name,
	}
	return fat
}

// GetRawDevice opens and returns a file-backed device without a cache.
func (fs *FileFAT) GetRawDevice() block.Device {
	f, err := os.OpenFile(fs.name, os.O_RDWR, 0666)
	if err != nil {
		fs.t.Fatal(err)
	}

	d, err := file.New(f, 512)
	if err != nil {
		fs.t.Fatal(err)
	}
	return d
}

// GetDevice opens and returns a file-backed device which corresponds to the image.
func (fs *FileFAT) GetDevice() *thinio.Conductor {
	f, err := os.OpenFile(fs.name, os.O_RDWR, 0666)
	if err != nil {
		fs.t.Fatal(err)
	}

	d, err := file.New(f, 512)
	if err != nil {
		fs.t.Fatal(err)
	}

	return thinio.NewConductor(d, 5012)
}

// RmfsFAT removes the FAT filesystem image created by MkfsFAT.
func (fs *FileFAT) RmfsFAT() {
	err := os.Remove(fs.name)
	if err != nil {
		fs.t.Fatal(err)
	}
}
