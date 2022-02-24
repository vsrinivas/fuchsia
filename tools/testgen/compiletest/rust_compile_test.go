// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package compiletest

import (
	"flag"
	"os"
	"testing"
)

var (
	cm = flag.String("cm", "", "Path to cm file")
)

func Test_Compile(t *testing.T) {
	if _, err := os.Stat(*cm); os.IsNotExist(err) {
		t.Fatalf("Invalid cm path %q err: %s", *cm, err)
	}
}
