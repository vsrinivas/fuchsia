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

package direntry

import (
	"errors"
	"testing"
	"time"
	"unsafe"

	"fuchsia.googlesource.com/thinfs/lib/fs"
)

func TestDirentrySize(t *testing.T) {
	short := &shortDirentry{}
	size := unsafe.Sizeof(*short)
	if size != DirentrySize {
		t.Fatalf("Unexpected short direntry size: %d (expected %d)", size, DirentrySize)
	}
	align := unsafe.Alignof(*short)
	if align != 1 {
		t.Fatalf("Unexpected short direntry alignment: %d (expected %d)", align, 1)
	}

	long := &longDirentry{}
	size = unsafe.Sizeof(*long)
	if size != DirentrySize {
		t.Fatalf("Unexpected long direntry size: %d (expected %d)", size, DirentrySize)
	}
	align = unsafe.Alignof(*long)
	if align != 1 {
		t.Fatalf("Unexpected long direntry alignment: %d (expected %d)", align, 1)
	}
}

func checkEntry(t *testing.T, d *Dirent, goldName string, goldCluster uint32, goldAttr fs.FileType, goldSize uint32) {
	if d.Cluster != goldCluster {
		t.Fatalf("Expected cluster %d, saw %d", goldCluster, d.Cluster)
	} else if d.GetType() != goldAttr {
		t.Fatalf("Expected type %d, saw %d", goldAttr, d.GetType())
	} else if d.Filename != goldName {
		t.Fatalf("Expected name %s, saw %s", goldName, d.Filename)
	} else if d.Size != goldSize {
		t.Fatalf("Expected size %d, saw %d", goldSize, d.Size)
	}

	if d.IsFree() {
		t.Fatal("By default, direntries should not be free")
	} else if d.IsLastFree() {
		t.Fatal("By default, direntries should not be last free")
	}
}

func TestUpdateWriteTime(t *testing.T) {
	goldName := "FILE.TXT"
	goldCluster := uint32(1243)
	goldAttr := fs.FileTypeRegularFile
	goldSize := uint32(5)
	d := New(goldName, goldCluster, goldAttr)
	d.Size = goldSize
	checkEntry(t, d, goldName, goldCluster, goldAttr, goldSize)

	checkTime := func(newTime, goldTime time.Time) {
		// Set the time
		d.WriteTime = newTime

		// Serialize and deserialize to simulate writing to disk
		callback := func(i uint) ([]byte, error) {
			panic("Callback should not be necessary")
		}
		buf, err := d.Serialize(callback)
		if err != nil {
			t.Fatal(err)
		}

		// Load dirent
		callback = func(i uint) ([]byte, error) {
			return buf, nil
		}
		d, _, err = LoadDirent(callback, 0)
		if err != nil {
			t.Fatal(err)
		}

		// FAT timestamps use two second granularity. Compare the "on disk" time with the actual time
		// we intended to write.
		diskTime := d.WriteTime
		if diskTime != goldTime {
			t.Fatalf("Invalid disk time (actual %s),  (expected %s)", diskTime, goldTime)
		}
	}

	// Test a normal time
	newTime := time.Now()
	goldTime := newTime.Truncate(time.Second * 2)
	checkTime(newTime, goldTime)

	// Test a time in the distant future
	newTime = time.Date(2108, 1, 1, 1, 1, 0, 0, time.Local)
	goldTime = time.Date(2107, 12, 31, 23, 59, 58, 0, time.Local)
	checkTime(newTime, goldTime)

	// Test a time in the extremely distant future
	newTime = time.Date(2200, 1, 1, 1, 1, 0, 0, time.Local)
	goldTime = time.Date(2107, 12, 31, 23, 59, 58, 0, time.Local)
	checkTime(newTime, goldTime)
}

func appendEmptyFile(directory []byte, name string) []byte {
	d := New(name, 0, fs.FileTypeRegularFile)
	callback := func(i uint) ([]byte, error) {
		if i >= uint(len(directory)/DirentrySize) {
			// Pretend we reached the end of the directory
			return LastFreeDirent(), nil
		}
		return directory[i*DirentrySize : (i+1)*DirentrySize], nil
	}
	buf, err := d.Serialize(callback)
	if err != nil {
		panic("Could not append empty file")
	}
	return append(directory, buf...)
}

func appendFree(directory []byte, lastFree bool) []byte {
	if lastFree {
		return append(directory, LastFreeDirent()...)
	}
	return append(directory, FreeDirent()...)
}

func checkedSerialize(t *testing.T, directory []byte, d *Dirent, goldSize int) []byte {
	callback := func(i uint) ([]byte, error) {
		return directory[i*DirentrySize : (i+1)*DirentrySize], nil
	}
	buf, err := d.Serialize(callback)
	if err != nil {
		t.Fatal(err)
	} else if len(buf) != goldSize {
		t.Fatalf("Expected serialized buffer to have length %d, but it had length %d", goldSize, len(buf))
	}
	return buf
}

func checkedLookup(t *testing.T, directory []byte, name string, goldIndex uint) *Dirent {
	callback := func(i uint) ([]byte, error) {
		return directory[i*DirentrySize : (i+1)*DirentrySize], nil
	}
	d, foundIndex, err := LookupDirent(callback, name)
	if err != nil {
		t.Fatal(err)
	} else if foundIndex != goldIndex {
		t.Fatalf("Found a direntry at an unexpected index %d (expected %d)", foundIndex, goldIndex)
	}
	return d
}

func TestSerializeAndLoadShort(t *testing.T) {
	goldName := "FILENAME.TXT"
	goldCluster := uint32(1)
	goldAttr := fs.FileTypeRegularFile
	goldSize := uint32(5)
	d := New(goldName, goldCluster, goldAttr)
	d.Size = goldSize
	checkEntry(t, d, goldName, goldCluster, goldAttr, goldSize)

	var directory []byte
	directory = appendFree(directory /* lastFree = */, true)
	buf := checkedSerialize(t, directory, d, DirentrySize)

	directory = make([]byte, 0)
	directory = append(directory, buf...)
	directory = appendFree(directory /* lastFree = */, true)
	foundDirent := checkedLookup(t, directory, goldName, 0)
	checkEntry(t, foundDirent, goldName, goldCluster, goldAttr, goldSize)
}

func TestSerializeAndLoadShortAtOffset(t *testing.T) {
	goldName := "FILE.FOO"
	goldCluster := uint32(123)
	goldAttr := fs.FileTypeRegularFile
	goldSize := uint32(0)
	d := New(goldName, goldCluster, goldAttr)
	d.Size = goldSize
	checkEntry(t, d, goldName, goldCluster, goldAttr, goldSize)

	var directory []byte
	directory = appendEmptyFile(directory, "A.TXT")
	directory = appendEmptyFile(directory, "B.TXT")
	directory = appendEmptyFile(directory, "The quick brown.fox")
	directory = appendFree(directory /* lastFree = */, false)
	directory = appendFree(directory /* lastFree = */, true)
	buf := checkedSerialize(t, directory, d, DirentrySize)

	directory = make([]byte, 0)
	directory = appendEmptyFile(directory, "A.TXT")               // Index 0
	directory = appendEmptyFile(directory, "B.TXT")               // Index 1
	directory = appendEmptyFile(directory, "The quick brown.fox") // Index 2, 3, 4
	directory = append(directory, buf...)                         // Index 5
	directory = appendFree(directory /* lastFree = */, true)
	foundDirent := checkedLookup(t, directory, goldName, 5)
	checkEntry(t, foundDirent, goldName, goldCluster, goldAttr, goldSize)
}

func TestSerializeAndLoadLong(t *testing.T) {
	goldName := "The quick brown.fox"
	goldCluster := uint32(2)
	goldAttr := fs.FileTypeRegularFile
	goldSize := uint32(5)
	d := New(goldName, goldCluster, goldAttr)
	d.Size = goldSize
	checkEntry(t, d, goldName, goldCluster, goldAttr, goldSize)

	var directory []byte
	directory = appendFree(directory /* lastFree = */, true)
	buf := checkedSerialize(t, directory, d, DirentrySize*3) // 2 for LFN, 1 for short name

	directory = make([]byte, 0)
	directory = append(directory, buf...)
	directory = appendFree(directory /* lastFree = */, true)
	foundDirent := checkedLookup(t, directory, goldName, 0)
	checkEntry(t, foundDirent, goldName, goldCluster, goldAttr, goldSize)
}

func TestSerializeAndLoadLongAtOffset(t *testing.T) {
	goldName := ".FOOBAR"
	goldCluster := uint32(0x00112233)
	goldAttr := fs.FileTypeRegularFile
	goldSize := uint32(0)
	d := New(goldName, goldCluster, goldAttr)
	d.Size = goldSize
	checkEntry(t, d, goldName, goldCluster, goldAttr, goldSize)

	var directory []byte
	directory = appendEmptyFile(directory, ".foobar") // Index 0, 1
	directory = appendEmptyFile(directory, ".Foobar") // Index 2, 3
	directory = appendEmptyFile(directory, ".FOobar") // Index 4, 5
	directory = appendFree(directory /* lastFree = */, true)
	buf := checkedSerialize(t, directory, d, DirentrySize*2)

	directory = make([]byte, 0)
	directory = appendEmptyFile(directory, ".foobar")         // Index 0, 1
	directory = appendEmptyFile(directory, ".Foobar")         // Index 2, 3
	directory = appendEmptyFile(directory, ".FOobar")         // Index 4, 5
	directory = appendFree(directory /* lastFree = */, false) // Index 6
	directory = append(directory, buf...)                     // Index 7, 8
	directory = appendFree(directory /* lastFree = */, true)

	// We should be able to distinguish between the different variations of ".foobar".
	checkedLookup(t, directory, ".foobar", 0)
	checkedLookup(t, directory, ".Foobar", 2)
	checkedLookup(t, directory, ".FOobar", 4)
	foundDirent := checkedLookup(t, directory, goldName, 7)
	checkEntry(t, foundDirent, goldName, goldCluster, goldAttr, goldSize)
}

func TestLoadMissingOffset(t *testing.T) {
	var directory []byte
	directory = appendEmptyFile(directory, ".foobar") // Index 0, 1
	directory = appendEmptyFile(directory, ".Foobar") // Index 2, 3
	directory = appendEmptyFile(directory, ".FOobar") // Index 4, 5
	directory = appendFree(directory /* lastFree = */, true)

	// We should be able to distinguish between the different variations of ".foobar".
	checkedLookup(t, directory, ".foobar", 0)
	checkedLookup(t, directory, ".Foobar", 2)
	checkedLookup(t, directory, ".FOobar", 4)
	foundDirent := checkedLookup(t, directory, ".FooBar", 0)
	if foundDirent != nil {
		t.Fatal("Expected that dirent would not be found")
	}
}

func TestSerializeError(t *testing.T) {
	// Serialize with a completely broken callback
	d := New("This long filename will require a generation number (and callback)", 0, fs.FileTypeRegularFile)
	goldErr := errors.New("This is the first callback error")
	callback := func(i uint) ([]byte, error) {
		return nil, goldErr
	}
	_, err := d.Serialize(callback)
	if err != goldErr {
		t.Fatalf("Expected error %s, saw %s", goldErr, err)
	}
}

func TestLookupCallbackError(t *testing.T) {
	goldErr := errors.New("This is a callback error")
	callback := func(i uint) ([]byte, error) {
		return nil, goldErr
	}

	_, _, err := LookupDirent(callback, "foobar")
	if err != goldErr {
		t.Fatalf("Expected error %s, saw %s", goldErr, err)
	}
}

func TestLoadLongDirentError(t *testing.T) {
	var directory []byte
	directory = appendEmptyFile(directory, "The quick brown.fox") // Index 0, 1, 2
	directory = appendFree(directory /* lastFree = */, true)
	callback := func(i uint) ([]byte, error) {
		return directory[i*DirentrySize : (i+1)*DirentrySize], nil
	}
	// Try reading a long direntry from an invalid starting location.
	_, _, err := LoadDirent(callback, 1)
	if err != errLongDirentry {
		t.Fatalf("Expected err %s, saw %s", errLongDirentry, err)
	}

	// Try reading a long direntry when the corresponding short direntry is free.
	copy(directory[DirentrySize*2:DirentrySize*3], FreeDirent())
	_, _, err = LoadDirent(callback, 0)
	if err != errLongDirentry {
		t.Fatalf("Expected err %s, saw %s", errLongDirentry, err)
	}
}
