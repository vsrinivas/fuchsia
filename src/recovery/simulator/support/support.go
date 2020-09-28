// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package support

import (
	"crypto/rand"
	"fmt"
	"os"
	"path/filepath"
	"testing"
)

func ZbiPath(t *testing.T) string {
	ex, err := os.Executable()
	if err != nil {
		t.Fatal(err)
		return ""
	}
	exPath := filepath.Dir(ex)
	// TODO(fxbug.dev/47555): get the path from a build API instead.
	return filepath.Join(exPath, "../obj/build/images/recovery/recovery-eng.zbi")
}

func RandomTokenAsString() string {
	b := make([]byte, 32)
	_, err := rand.Read(b)
	if err != nil {
		panic(err)
	}

	ret := ""
	for i := 0; i < 32; i++ {
		ret += fmt.Sprintf("%x", b[i])
	}
	return ret
}
