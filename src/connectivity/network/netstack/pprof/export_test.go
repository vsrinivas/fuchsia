// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain

package pprof

import (
	"io"
	"strings"
	"syscall/zx"
	"testing"
)

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
