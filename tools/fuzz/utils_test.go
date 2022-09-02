// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fuzz

import (
	"io"
	"strings"
	"testing"
)

func TestParallelMultiReader(t *testing.T) {
	r1, w1 := io.Pipe()
	r2, w2 := io.Pipe()
	line1 := "hello\n"
	line2 := "world\n"

	combined := parallelMultiReader(r1, r2)
	io.WriteString(w1, line1)
	io.WriteString(w2, line2)
	w1.Close()
	w2.Close()

	out, err := io.ReadAll(combined)
	if err != nil {
		t.Fatalf("error reading: %s", err)
	}

	if !strings.Contains(string(out), line1) || !strings.Contains(string(out), line2) {
		t.Fatalf("unexpected output: %s", out)
	}
}
