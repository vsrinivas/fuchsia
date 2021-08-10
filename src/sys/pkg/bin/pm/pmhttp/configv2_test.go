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

	repo_structs "go.fuchsia.dev/fuchsia/src/sys/pkg/lib/repo"
)

func TestConfigV2(t *testing.T) {
	repoDir := t.TempDir()

	repository, err := repo.New(repoDir, t.TempDir())
	if err != nil {
		t.Fatal(err)
	}

	if err := repository.Init(); err != nil {
		t.Fatal(err)
	}

	if err := repository.AddTargets([]string{}, json.RawMessage{}); err != nil {
		t.Fatal(err)
	}

	if err := repository.CommitUpdates(false); err != nil {
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

		server := NewConfigServerV2(func() []byte { return rootBytes }, false)
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

	t.Run("parse config with persistency flag set", func(t *testing.T) {
		var signed tuf_data.Signed
		var root tuf_data.Root
		if err := json.Unmarshal(rootBytes, &signed); err != nil {
			t.Fatal(err)
		}
		if err := json.Unmarshal(signed.Signed, &root); err != nil {
			t.Fatal(err)
		}

		server := NewConfigServerV2(func() []byte { return rootBytes }, true)
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

		if config.StorageType != repo_structs.Persistent {
			t.Fatalf("expected storage type to be %v, not %v", repo_structs.Persistent, config.StorageType)
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

		server := NewConfigServerV2(func() []byte { return badRootBytes }, false)
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

		server := NewConfigServerV2(func() []byte { return badRootBytes }, false)
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

		server := NewConfigServerV2(func() []byte { return badRootBytes }, false)
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

		server := NewConfigServerV2(func() []byte { return badRootBytes }, false)
		if _, err := server.parseConfig("http://localhost"); !errors.Is(err, OutOfRangeError) {
			t.Fatalf("expected error to be out of range, not %v", err)
		}
	})
}

func TestBuildRepoUrl(t *testing.T) {
	testCases := []struct {
		url  string
		want string
	}{
		{
			url:  "fuchsia-pkg://devhost",
			want: "fuchsia-pkg://devhost",
		},
		{
			url:  "fuchsia-pkg://DEVHOST",
			want: "fuchsia-pkg://devhost",
		},
		{
			url:  "http://fuchsia.com",
			want: "fuchsia-pkg://fuchsia.com",
		},
		{
			url:  "https://fuchsia.com/packages",
			want: "fuchsia-pkg://fuchsia.com-packages",
		},
	}

	for _, tc := range testCases {
		got := buildRepoUrl(tc.url)
		if got != tc.want {
			t.Errorf("buildRepoUrl(%v) returned %v, wanted %v", tc.url, got, tc.want)
		}
	}
}
