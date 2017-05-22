// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package genkey

import (
	"io/ioutil"
	"os"
	"path/filepath"
	"testing"
)

func TestRun(t *testing.T) {
	d, err := ioutil.TempDir("", t.Name())
	defer os.RemoveAll(d)
	if err != nil {
		t.Fatal(err)
	}

	if err := Run(d); err != nil {
		t.Fatal(err)
	}

	if _, err := os.Stat(filepath.Join(d, "key")); err != nil {
		t.Errorf("genkey didn't write a key!")
	}
}
