// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package pmhttp

import (
	"encoding/json"
	"errors"
	"io/ioutil"
	"math"
	"path/filepath"
	"testing"

	tuf_data "github.com/theupdateframework/go-tuf/data"

	"go.fuchsia.dev/fuchsia/src/sys/pkg/bin/pm/repo"
)

func TestConfig(t *testing.T) {
	repoDir := t.TempDir()

	repo, err := repo.New(repoDir, t.TempDir())
	if err != nil {
		t.Fatal(err)
	}

	if err := repo.Init(); err != nil {
		t.Fatal(err)
	}

	if err := repo.AddTargets([]string{}, json.RawMessage{}); err != nil {
		t.Fatal(err)
	}

	if err := repo.CommitUpdates(false); err != nil {
		t.Fatal(err)
	}

	rootBytes, err := ioutil.ReadFile(filepath.Join(repoDir, "repository", "root.json"))
	if err != nil {
		t.Fatal(err)
	}

	t.Run("parse config", func(t *testing.T) {
		var signed tuf_data.Signed
		var root tuf_data.Root
		if err := json.Unmarshal(rootBytes, &signed); err != nil {
			t.Fatal(err)
		}
		if err := json.Unmarshal(signed.Signed, &root); err != nil {
			t.Fatal(err)
		}

		server := NewConfigServer(func() []byte { return rootBytes }, "")
		config, err := server.parseConfig("http://localhost")
		if err != nil {
			t.Fatal(err)
		}

		if root.Version != int(config.RootVersion) {
			t.Fatalf("expected root version to be %v, not %v", root.Version, config.RootVersion)
		}

		rootRole, ok := root.Roles["root"]
		if !ok {
			t.Fatalf("root does not have root role")
		}

		if rootRole.Threshold != int(config.RootThreshold) {
			t.Fatalf("expected root version to be %v, not %v", root.Version, config.RootThreshold)
		}
	})

	t.Run("parse config errs with underflow root version", func(t *testing.T) {
		var signed tuf_data.Signed
		var root tuf_data.Root
		if err := json.Unmarshal(rootBytes, &signed); err != nil {
			t.Fatal(err)
		}
		if err := json.Unmarshal(signed.Signed, &root); err != nil {
			t.Fatal(err)
		}

		root.Version = -1
		signed.Signed, err = json.Marshal(root)
		if err != nil {
			t.Fatal(err)
		}
		badRootBytes, err := json.Marshal(signed)
		if err != nil {
			t.Fatal(err)
		}

		server := NewConfigServer(func() []byte { return badRootBytes }, "")
		if _, err := server.parseConfig("http://localhost"); !errors.Is(err, OutOfRangeError) {
			t.Fatalf("expected error to be out of range, not %v", err)
		}
	})

	t.Run("parse config errs with overflow root version", func(t *testing.T) {
		var signed tuf_data.Signed
		var root tuf_data.Root
		if err := json.Unmarshal(rootBytes, &signed); err != nil {
			t.Fatal(err)
		}
		if err := json.Unmarshal(signed.Signed, &root); err != nil {
			t.Fatal(err)
		}

		root.Version = math.MaxUint32 + 1
		signed.Signed, err = json.Marshal(root)
		if err != nil {
			t.Fatal(err)
		}
		badRootBytes, err := json.Marshal(signed)
		if err != nil {
			t.Fatal(err)
		}

		server := NewConfigServer(func() []byte { return badRootBytes }, "")
		if _, err := server.parseConfig("http://localhost"); !errors.Is(err, OutOfRangeError) {
			t.Fatalf("expected error to be out of range, not %v", err)
		}
	})

	t.Run("parse config errs with underflow root threshold", func(t *testing.T) {
		var signed tuf_data.Signed
		var root tuf_data.Root
		if err := json.Unmarshal(rootBytes, &signed); err != nil {
			t.Fatal(err)
		}
		if err := json.Unmarshal(signed.Signed, &root); err != nil {
			t.Fatal(err)
		}

		root.Roles["root"].Threshold = -1
		signed.Signed, err = json.Marshal(root)
		if err != nil {
			t.Fatal(err)
		}
		badRootBytes, err := json.Marshal(signed)
		if err != nil {
			t.Fatal(err)
		}

		server := NewConfigServer(func() []byte { return badRootBytes }, "")
		if _, err := server.parseConfig("http://localhost"); !errors.Is(err, OutOfRangeError) {
			t.Fatalf("expected error to be out of range, not %v", err)
		}
	})

	t.Run("parse config errs with overrflow root threshold", func(t *testing.T) {
		var signed tuf_data.Signed
		var root tuf_data.Root
		if err := json.Unmarshal(rootBytes, &signed); err != nil {
			t.Fatal(err)
		}
		if err := json.Unmarshal(signed.Signed, &root); err != nil {
			t.Fatal(err)
		}

		root.Roles["root"].Threshold = math.MaxUint32 + 1
		signed.Signed, err = json.Marshal(root)
		if err != nil {
			t.Fatal(err)
		}
		badRootBytes, err := json.Marshal(signed)
		if err != nil {
			t.Fatal(err)
		}

		server := NewConfigServer(func() []byte { return badRootBytes }, "")
		if _, err := server.parseConfig("http://localhost"); !errors.Is(err, OutOfRangeError) {
			t.Fatalf("expected error to be out of range, not %v", err)
		}
	})
}
