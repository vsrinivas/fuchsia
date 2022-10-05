// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain

package pprof

import (
	"errors"
	"io"
	"os"
	"path/filepath"
	"strings"
	"syscall/zx"
	"testing"
)

func TestStdioFile(t *testing.T) {
	dir, err := os.MkdirTemp("/tmp", "*")
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() {
		if err := os.RemoveAll(dir); err != nil {
			t.Error(err)
		}
	})

	path := filepath.Join(dir, "testFile")

	// It should be OK to define the file before it actually exists on disk.
	file := newStdioFile(path)

	{
		reader, length, err := file.GetReader()
		if !errors.Is(err, os.ErrNotExist) {
			t.Errorf("got %s, want %s", err, os.ErrNotExist)
		}
		if err == nil {
			if err := reader.Close(); err != nil {
				t.Error(err)
			}
		}
		if length != 0 {
			t.Errorf("got reader length %d, expected 0", length)
		}
	}

	content := "Hello World"

	if err := os.WriteFile(path, []byte(content), os.ModePerm); err != nil {
		t.Fatal(err)
	}

	reader, length, err := file.GetReader()
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() {
		if err := reader.Close(); err != nil {
			t.Error(err)
		}
	})

	if length != uint64(len(content)) {
		t.Errorf("got length %d, expected %d", length, len(content))
	}

	gotContentBytes, err := io.ReadAll(reader)
	gotContent := string(gotContentBytes)

	if gotContent != content {
		t.Errorf("got content %s, expected %s", gotContent, content)
	}
}

func TestNowFile(t *testing.T) {
	reader, len, err := nowFile.File.GetReader()
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() {
		if err := reader.Close(); err != nil {
			t.Error(err)
		}
	})

	if len == 0 {
		t.Errorf("expected nowFile.File.GetReader() to have length > 0")
	}

	bytes, err := io.ReadAll(reader)
	if err != nil {
		t.Error(err)
	}

	// Check that "goroutine" appears somewhere within the file (meant to be
	// low-effort check that this is actually a pprof profile):
	content := string(bytes)
	if !strings.Contains(content, "goroutine") {
		t.Errorf("\"goroutine\" did not appear within %q", content)
	}

	vmo := reader.GetVMO()
	if vmo == nil {
		t.Errorf("got nil VMO from reader.GetVMO()")
	} else if *vmo == zx.VMO(zx.HandleInvalid) {
		t.Errorf("got invalid VMO from reader.GetVMO()")
	}
}
