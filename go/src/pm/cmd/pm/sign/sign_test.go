// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package sign

import (
	"io/ioutil"
	"os"
	"path/filepath"
	"sort"
	"testing"

	"golang.org/x/crypto/ed25519"

	"fuchsia.googlesource.com/pm/cmd/pm/genkey"
	initcmd "fuchsia.googlesource.com/pm/cmd/pm/init"
	"fuchsia.googlesource.com/pm/cmd/pm/update"
	"fuchsia.googlesource.com/pm/testpackage"
)

func TestRun(t *testing.T) {
	d, err := testpackage.New()
	defer os.RemoveAll(d)
	if err != nil {
		t.Fatal(err)
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

	if _, err := os.Stat(filepath.Join(d, "meta", "signature")); !os.IsNotExist(err) {
		t.Fatal("unexpected signature file created during test setup")
	}
	if _, err := os.Stat(filepath.Join(d, "meta", "pubkey")); !os.IsNotExist(err) {
		t.Fatal("unexpected pubkey file created during test setup")
	}

	if err := Run(d, key); err != nil {
		t.Fatal(err)
	}

	var msg []byte
	metaFiles, err := filepath.Glob(filepath.Join(d, "meta", "*"))
	if err != nil {
		t.Fatal(err)
	}
	sort.Strings(metaFiles)
	for _, f := range metaFiles {
		switch filepath.Base(f) {
		case "signature":
			continue
		}

		buf, err := ioutil.ReadFile(f)
		if err != nil {
			t.Fatal(err)
		}
		msg = append(msg, buf...)
	}

	f, err := os.Open(filepath.Join(d, "meta", "signature"))
	if err != nil {
		t.Fatal(err)
	}
	sig := make([]byte, 1024)
	n, err := f.Read(sig)
	if err != nil {
		t.Fatal(err)
	}
	sig = sig[:n]

	buf, err = ioutil.ReadFile(filepath.Join(d, "meta", "pubkey"))
	if err != nil {
		t.Fatal(err)
	}
	pubkey := ed25519.PublicKey(buf)

	if !ed25519.Verify(pubkey, msg, sig) {
		t.Fatal("signature verification failed")
	}
}
