// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package pkgfs

import (
	"fmt"
	"testing"
	"thinfs/fs"
	"time"
)

var (
	entries = []string{
		"1111111111111111111111111111111111111111111111111111111111111111",
		"2222222222222222222222222222222222222222222222222222222222222222",
		"3333333333333333333333333333333333333333333333333333333333333333",
		"4444444444444444444444444444444444444444444444444444444444444444",
		"5555555555555555555555555555555555555555555555555555555555555555",
		"6666666666666666666666666666666666666666666666666666666666666666",
	}

	file = []byte(
		`1111111111111111111111111111111111111111111111111111111111111111
2222222222222222222222222222222222222222222222222222222222222222
3333333333333333333333333333333333333333333333333333333333333333
4444444444444444444444444444444444444444444444444444444444444444
5555555555555555555555555555555555555555555555555555555555555555
6666666666666666666666666666666666666666666666666666666666666666
`)
)

// TestWholeFileRead creates a buffer that is size of the expected file output
// and tries to read this from the file in one call.
func TestWholeFileRead(t *testing.T) {
	// read the whole file
	if lineSize != int64(len(entries[0])+1) {
		t.Fatal("test setup error: entries length doesn't match expected line size")
	}

	bufSize := len(file)
	buf := make([]byte, bufSize)

	vf := newTestValidationFile(entries)
	r, err := vf.Read(buf, 0, fs.WhenceFromCurrent)
	if err != nil {
		t.Fatalf("read failed: %s", err)
	}
	if r != len(file) {
		t.Fatalf("unexpected read length, expected %d, got %d", bufSize, r)
	}

	if err = compareBuffers(file, buf, 0, t); err != nil {
		t.Fatalf("%s", err)
	}

	r, err = vf.Read(buf[:1], 0, fs.WhenceFromCurrent)
	if err != fs.ErrEOF {
		t.Fatalf("EOF expected, instead found: %s", err)
	}
	if r != 0 {
		t.Fatalf("file read beyond end of file reported length %d", r)
	}
}

// TestOverRead creates a buffer larger than the whole file and passes that
// buffer into the read call.
func TestOverRead(t *testing.T) {
	// try to read more than the whole file
	if lineSize != int64(len(entries[0])+1) {
		t.Fatal("test setup error: entries length doesn't match expected line size")
	}

	bufSize := len(file) * 2
	buf := make([]byte, bufSize)

	vf := newTestValidationFile(entries)
	r, err := vf.Read(buf, 0, fs.WhenceFromCurrent)
	if err != fs.ErrEOF {
		t.Fatalf("EOF expected, instead found: %s", err)
	}
	if r != len(file) {
		t.Fatalf("unexpected read length, expected %d, got %d", bufSize, r)
	}

	if err = compareBuffers(file, buf[:len(file)], 0, t); err != nil {
		t.Fatalf("%s", err)
	}
}

// TestMultipleRead tries to read the first half of the file in one call and
// the remainder in the second call.
func TestMultipleRead(t *testing.T) {
	if lineSize != int64(len(entries[0])+1) {
		t.Fatal("test setup error: entries length doesn't match expected line size")
	}

	// read half the file and then the next half
	bufSize := len(file)
	buf := make([]byte, bufSize)

	vf := newTestValidationFile(entries)
	firstHalf := len(buf) / 2
	r, err := vf.Read(buf[0:firstHalf], 0, fs.WhenceFromCurrent)
	if r != firstHalf {
		t.Fatalf("unexpected read length reading first half of file, expected %d, got %d", firstHalf, r)
	}

	if err = compareBuffers(file[:firstHalf], buf[:firstHalf], 0, t); err != nil {
		t.Fatalf("%s", err)
	}

	remaining := len(buf) - firstHalf
	r, err = vf.Read(buf[firstHalf:], 0, fs.WhenceFromCurrent)
	if r != remaining {
		t.Fatalf("unexpected read length reading first half of file, expected %d, got %d", remaining, r)
	}

	if err = compareBuffers(file[firstHalf:], buf[firstHalf:], firstHalf, t); err != nil {
		t.Fatalf("%s", err)
	}
}

// TestSeekToLineBoundary tests what happens if we first seek to the end of an
// entry, before the newline, and then read a line's worth of bytes.
func TestSeekToLineBounary(t *testing.T) {
	buf := make([]byte, lineSize)
	seekOffset := lineSize - 1

	vf := newTestValidationFile(entries)
	r, err := vf.Read(buf, seekOffset, fs.WhenceFromCurrent)
	if r != len(buf) {
		t.Fatalf("unexpected read length, expected %d got %d", len(buf), r)
	}

	if err = compareBuffers(file[seekOffset:], buf, int(seekOffset), t); err != nil {
		t.Fatalf("%s", err)
	}
}

// TestNewLineAdded tests what happens if we read a whole entry without the new
// line and then reads one more byte, which should be the new line
func TestNewLineAdded(t *testing.T) {
	// read 64 bytes, then read one more
	buf := make([]byte, lineSize)
	vf := newTestValidationFile(entries)
	firstRead := len(buf) - 1
	r, err := vf.Read(buf[0:firstRead], 0, fs.WhenceFromCurrent)

	if r != firstRead {
		t.Fatalf("unexpected read length reading first part of entry, expected %d, got %d",
			firstRead, r)
	}

	if err = compareBuffers(file[:firstRead], buf[:firstRead], 0, t); err != nil {
		t.Fatalf("%s", err)
	}

	r, err = vf.Read(buf[firstRead:], 0, fs.WhenceFromCurrent)
	if r != len(buf)-firstRead {
		t.Fatalf("unexpected read length reading second part of entry, expecting %d, got %d",
			len(buf)-firstRead, r)
	}

	if err = compareBuffers(file[firstRead:], buf[firstRead:], firstRead, t); err != nil {
		t.Fatalf("%s", err)
	}
}

// TestReadsAcrossEntries reads half an entry and then a full entry's worth of
// data.
func TestReadsAcrossEntries(t *testing.T) {
	// read half of an entry, read a full entry's worth

	if lineSize != int64(len(entries[0])+1) {
		t.Fatal("test setup error: entries length doesn't match expected line size")
	}

	buf := make([]byte, lineSize)
	vf := newTestValidationFile(entries)
	firstRead := len(buf) / 2
	r, err := vf.Read(buf[0:firstRead], 0, fs.WhenceFromCurrent)

	if r != firstRead {
		t.Fatalf("unexpected read length reading first half of entry, expected %d, got %d",
			firstRead, r)
	}

	if err = compareBuffers(file[:firstRead], buf[:firstRead], 0, t); err != nil {
		t.Fatalf("%s", err)
	}

	r, err = vf.Read(buf[firstRead:], 0, fs.WhenceFromCurrent)
	if r != len(buf)-firstRead {
		t.Fatalf("unexpected read length reading second half of entry, expecting %d, got %d",
			len(buf)-firstRead, r)
	}

	if err = compareBuffers(file[firstRead:], buf[firstRead:], firstRead, t); err != nil {
		t.Fatalf("%s", err)
	}
}

// TestBackwardSeek checks what happens if we read the whole file and then seek
// backward an entry and a half.
func TestBackwardSeek(t *testing.T) {
	if lineSize != int64(len(entries[0])+1) {
		t.Fatal("test setup error: entries length doesn't match expected line size")
	}
	bufSize := len(file)
	buf := make([]byte, bufSize)

	vf := newTestValidationFile(entries)
	r, err := vf.Read(buf, 0, fs.WhenceFromCurrent)
	if err != nil {
		t.Fatalf("read failed: %s", err)
	}
	if r != len(file) {
		t.Fatalf("unexpected read length, expected %d, got %d", bufSize, r)
	}

	if err = compareBuffers(file, buf[:len(file)], 0, t); err != nil {
		t.Fatalf("%s", err)
	}

	secondReadSz := lineSize * 3 / 2
	r, err = vf.Read(buf, 0-secondReadSz, fs.WhenceFromCurrent)
	if err != fs.ErrEOF {
		t.Fatalf("read expected EOF, but got: %s", err)
	}
	if int64(r) != secondReadSz {
		t.Fatalf("unexpected read length, expected %d, got %d", secondReadSz, r)
	}
	fileOffset := len(file) - int(secondReadSz)
	if err = compareBuffers(file[fileOffset:], buf[:secondReadSz], fileOffset, t); err != nil {
		t.Fatalf("%s", err)
	}
}

func newTestValidationFile(entries []string) *validationFile {
	return &validationFile{
		unsupportedFile: unsupportedFile("testing"),
		entries:         entries,
		statTime:        time.Now(),
	}
}

func compareBuffers(expect, actual []byte, off int, t *testing.T) error {
	var err error
	for i, b := range actual {
		if expect[i] != b {
			err = fmt.Errorf("buffers differ")
			t.Errorf("read file differs at index %d, expected %q, got %q", off+i, expect[i], b)
		}
	}
	return err
}
