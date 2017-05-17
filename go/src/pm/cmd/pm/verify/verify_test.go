// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package verify

import (
	"io/ioutil"
	"os"
	"path/filepath"
	"testing"

	"golang.org/x/crypto/ed25519"

	"fuchsia.googlesource.com/pm/cmd/pm/genkey"
	initcmd "fuchsia.googlesource.com/pm/cmd/pm/init"
	"fuchsia.googlesource.com/pm/cmd/pm/sign"
	"fuchsia.googlesource.com/pm/cmd/pm/update"
)

func TestRun(t *testing.T) {
	d, err := ioutil.TempDir("", t.Name())
	if err != nil {
		t.Fatal(err)
	}
	defer os.Remove(d)

	packageFiles := []string{"a", "b", "dir/c"}
	for _, f := range packageFiles {
		f = filepath.Join(d, f)

		err := os.MkdirAll(filepath.Dir(f), os.ModePerm)
		if err != nil {
			t.Fatal(err)
		}
		f, err := os.Create(f)
		if err != nil {
			t.Fatal(err)
		}
		err = f.Close()
		if err != nil {
			t.Fatal(err)
		}
	}

	if err := genkey.Run(d); err != nil {
		t.Fatal(err)
	}
	buf, err := ioutil.ReadFile(filepath.Join(d, "key"))
	if err != nil {
		t.Fatal(err)
	}
	key := ed25519.PrivateKey(buf)

	if err := initcmd.Run(d); err != nil {
		t.Fatal(err)
	}
	if err := update.Run(d); err != nil {
		t.Fatal(err)
	}
	if err := sign.Run(d, key); err != nil {
		t.Fatal(err)
	}

	if err := Run(d); err != nil {
		t.Fatal(err)
	}

	// verification succeeded

	// truncate contents file to invalidate the verification input
	f, err := os.Create(filepath.Join(d, "meta", "contents"))
	if err != nil {
		t.Fatal(err)
	}
	f.Close()

	if err := Run(d); err != ErrVerificationFailed {
		t.Fatalf("got %v, want %v", err, ErrVerificationFailed)
	}
}
