// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package far

import (
	"bytes"
	"encoding/binary"
	"encoding/hex"
	"fmt"
	"io"
	"io/ioutil"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"testing"
)

func TestWrite(t *testing.T) {
	files := []string{"a", "b", "dir/c"}
	d := create(t, files)
	defer os.Remove(d)

	inputs := map[string]string{}
	for _, path := range files {
		inputs[path] = filepath.Join(d, path)
	}

	w := bytes.NewBuffer(nil)
	if err := Write(w, inputs); err != nil {
		t.Fatal(err)
	}

	far := w.Bytes()

	if !bytes.Equal(far[0:8], []byte(Magic)) {
		t.Errorf("got %x, want %x", far[0:8], []byte(Magic))
	}

	if !bytes.Equal(far, exampleArchive()) {
		t.Errorf("archives didn't match, got:\n%s", hex.Dump(far))
	}
}

func TestLengths(t *testing.T) {
	if want := 16; IndexLen != want {
		t.Errorf("IndexLen: got %d, want %d", IndexLen, want)
	}
	if want := 24; IndexEntryLen != want {
		t.Errorf("IndexEntryLen: got %d, want %d", IndexEntryLen, want)
	}
	if want := 32; DirectoryEntryLen != want {
		t.Errorf("DirectoryEntryLen: got %d, want %d", DirectoryEntryLen, want)
	}
}

// create makes a temporary directory and populates it with the files in
// the given slice. the files will contain their name as content. The path of
// the created directory is returned.
func create(t *testing.T, files []string) string {
	d, err := ioutil.TempDir("", t.Name())
	if err != nil {
		t.Fatal(err)
	}
	for _, path := range files {
		absPath := filepath.Join(d, path)
		if err := os.MkdirAll(filepath.Dir(absPath), os.ModePerm); err != nil {
			t.Fatal(err)
		}
		f, err := os.Create(absPath)
		if err != nil {
			t.Fatal(err)
		}
		if _, err := fmt.Fprintf(f, "%s\n", path); err != nil {
			t.Fatal(err)
		}
		if err := f.Close(); err != nil {
			t.Fatal(err)
		}
	}
	return d
}

func TestReader(t *testing.T) {
	_, err := NewReader(bytes.NewReader(exampleArchive()))
	if err != nil {
		t.Fatal(err)
	}

	corruptors := []func([]byte){
		// corrupt magic
		func(b []byte) { b[0] = 0 },
		// corrupt index length
		func(b []byte) { binary.LittleEndian.PutUint64(b[8:], 1) },
		// corrupt dirindex type
		func(b []byte) { b[IndexLen] = 255 },
		// TODO(raggi): corrupt index entry offset
		// TODO(raggi): remove index entries
	}

	for i, corrupt := range corruptors {
		far := exampleArchive()
		corrupt(far)
		_, err := NewReader(bytes.NewReader(far))
		if _, ok := err.(ErrInvalidArchive); !ok {
			t.Errorf("corrupt archive %d, got unexpected error %v", i, err)
		}
	}
}

func TestListFiles(t *testing.T) {
	far := exampleArchive()
	r, err := NewReader(bytes.NewReader(far))
	if err != nil {
		t.Fatal(err)
	}

	files := r.List()

	want := []string{"a", "b", "dir/c"}

	if len(files) != len(want) {
		t.Fatalf("listfiles: got %v, want %v", files, want)
	}

	sort.Strings(files)

	for i, want := range want {
		if got := files[i]; got != want {
			t.Errorf("listfiles: got %q, want %q at %d", got, want, i)
		}
	}
}

func TestReaderOpen(t *testing.T) {
	far := exampleArchive()
	r, err := NewReader(bytes.NewReader(far))
	if err != nil {
		t.Fatal(err)
	}

	if got, want := len(r.dirEntries), 3; got != want {
		t.Errorf("got %v, want %v", got, want)
	}

	for _, f := range []string{"a", "b", "dir/c"} {
		ra, err := r.Open(f)
		if err != nil {
			t.Fatal(err)
		}
		// buffer past the far content padding to check clamping of the readat range
		want := make([]byte, 10*1024)
		copy(want, []byte(f))
		want[len(f)] = '\n'
		got := make([]byte, 10*1024)

		n, err := ra.ReadAt(got, 0)
		if err != nil {
			t.Fatal(err)
		}
		if want := len(f) + 1; n != want {
			t.Errorf("got %d, want %d", n, want)
		}
		if !bytes.Equal(got, want) {
			t.Errorf("got %x, want %x", got, want)
		}
	}

	ra, err := r.Open("a")
	if err != nil {
		t.Fatal(err)
	}
	// ensure that negative offsets are rejected
	n, err := ra.ReadAt(make([]byte, 10), -10)
	if err != io.EOF || n != 0 {
		t.Errorf("got %d %v, want %d, %v", n, err, 0, io.EOF)
	}
	// ensure that offsets beyond length are rejected
	n, err = ra.ReadAt(make([]byte, 10), 10)
	if err != io.EOF || n != 0 {
		t.Errorf("got %d %v, want %d, %v", n, err, 0, io.EOF)
	}
}
func TestReaderReadFile(t *testing.T) {
	far := exampleArchive()
	r, err := NewReader(bytes.NewReader(far))
	if err != nil {
		t.Fatal(err)
	}

	if got, want := len(r.dirEntries), 3; got != want {
		t.Errorf("got %v, want %v", got, want)
	}

	for _, f := range []string{"a", "b", "dir/c"} {
		got, err := r.ReadFile(f)
		if err != nil {
			t.Fatal(err)
		}
		// buffer past the far content padding to check clamping of the readat range
		want := []byte(f + "\n")
		if !bytes.Equal(got, want) {
			t.Errorf("got %x, want %x", got, want)
		}
	}

	ra, err := r.Open("a")
	if err != nil {
		t.Fatal(err)
	}
	// ensure that negative offsets are rejected
	n, err := ra.ReadAt(make([]byte, 10), -10)
	if err != io.EOF || n != 0 {
		t.Errorf("got %d %v, want %d, %v", n, err, 0, io.EOF)
	}
	// ensure that offsets beyond length are rejected
	n, err = ra.ReadAt(make([]byte, 10), 10)
	if err != io.EOF || n != 0 {
		t.Errorf("got %d %v, want %d, %v", n, err, 0, io.EOF)
	}
}

func TestReadEmpty(t *testing.T) {
	r, err := NewReader(bytes.NewReader(emptyArchive()))
	if err != nil {
		t.Fatal(err)
	}
	if len(r.List()) != 0 {
		t.Error("empty archive should contain no files")
	}
}

func TestIsFAR(t *testing.T) {
	type args struct {
		r io.Reader
	}
	tests := []struct {
		name string
		args args
		want bool
	}{
		{
			name: "valid magic",
			args: args{strings.NewReader(Magic)},
			want: true,
		},
		{
			name: "empty",
			args: args{strings.NewReader("")},
			want: false,
		},
		{
			name: "not magic",
			args: args{strings.NewReader("ohai")},
			want: false,
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			if got := IsFAR(tt.args.r); got != tt.want {
				t.Errorf("IsFAR() = %v, want %v", got, tt.want)
			}
		})
	}
}

// exampleArchive produces an archive similar to far(1) output
func exampleArchive() []byte {
	b := make([]byte, 16384)
	copy(b, []byte{
		0xc8, 0xbf, 0x0b, 0x48, 0xad, 0xab, 0xc5, 0x11, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x44, 0x49, 0x52, 0x2d, 0x2d, 0x2d, 0x2d, 0x2d, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x44, 0x49, 0x52, 0x4e, 0x41, 0x4d, 0x45, 0x53,
		0xa0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x02, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x61, 0x62, 0x64, 0x69, 0x72, 0x2f, 0x63, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	})
	copy(b[4096:], []byte("a\n"))
	copy(b[8192:], []byte("b\n"))
	copy(b[12288:], []byte("dir/c\n"))
	return b
}

func emptyArchive() []byte {
	return []byte{0xc8, 0xbf, 0xb, 0x48, 0xad, 0xab, 0xc5, 0x11, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0}
}
