// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package daemon

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"fidl/fuchsia/amber"

	"fuchsia.googlesource.com/merkle"
	tuf "github.com/flynn/go-tuf"
)

func TestSources(t *testing.T) {
	dir, err := ioutil.TempDir("", "amber-test")
	if err != nil {
		panic(err)
	}
	defer os.RemoveAll(dir)

	d, err := NewDaemon(dir, "", "")
	if err != nil {
		panic(err)
	}

	t.Run("Add", func(t *testing.T) {
		if err := d.AddSource(&amber.SourceConfig{
			Id:      "addtest",
			RepoUrl: "http://localhost/addtest",
			RootKeys: []amber.KeyConfig{
				amber.KeyConfig{
					Type:  "ed25519",
					Value: "be0b983f7396da675c40c6b93e47fced7c1e9ea8a32a1fe952ba8f519760b307",
				},
			},
			StatusConfig: &amber.StatusConfig{Enabled: true},
		}); err != nil {
			t.Fatal(err)
		}

		if d.GetSources()["addtest"] == nil {
			t.Errorf("source missing after add, got %#v", d.GetSources())
		}
	})

	t.Run("Remove", func(t *testing.T) {
		if err := d.AddSource(&amber.SourceConfig{
			Id:      "removetest",
			RepoUrl: "http://localhost/removetest",
			RootKeys: []amber.KeyConfig{
				amber.KeyConfig{
					Type:  "ed25519",
					Value: "be0b983f7396da675c40c6b93e47fced7c1e9ea8a32a1fe952ba8f519760b307",
				},
			},
			StatusConfig: &amber.StatusConfig{Enabled: true},
		}); err != nil {
			t.Fatal(err)
		}

		if _, err := d.RemoveSource("removetest"); err != nil {
			t.Fatal(err)
		}
		if s := d.GetSources()["removetest"]; s != nil {
			t.Errorf("expected source to be removed, got %#v", s)
		}
	})

	t.Run("Disable", func(t *testing.T) {
		if err := d.AddSource(&amber.SourceConfig{
			Id:      "disabletest",
			RepoUrl: "http://localhost/disabletest",
			RootKeys: []amber.KeyConfig{
				amber.KeyConfig{
					Type:  "ed25519",
					Value: "be0b983f7396da675c40c6b93e47fced7c1e9ea8a32a1fe952ba8f519760b307",
				},
			},
			StatusConfig: &amber.StatusConfig{Enabled: true},
		}); err != nil {
			t.Fatal(err)
		}

		if d.GetActiveSources()["disabletest"] == nil {
			t.Fatal("expected source to be enabled initially")
		}

		d.DisableSource("disabletest")

		if d.GetActiveSources()["disabletest"] != nil {
			t.Fatal("expected source to be disabled")
		}
	})

	t.Run("Enable", func(t *testing.T) {
		if err := d.AddSource(&amber.SourceConfig{
			Id:      "enabletest",
			RepoUrl: "http://localhost/enabletest",
			RootKeys: []amber.KeyConfig{
				amber.KeyConfig{
					Type:  "ed25519",
					Value: "be0b983f7396da675c40c6b93e47fced7c1e9ea8a32a1fe952ba8f519760b307",
				},
			},
			StatusConfig: &amber.StatusConfig{Enabled: true},
		}); err != nil {
			t.Fatal(err)
		}

		d.DisableSource("enabletest")
		if d.GetActiveSources()["enabletest"] != nil {
			t.Fatal("expected source to be disabled")
		}

		d.EnableSource("enabletest")
		if d.GetActiveSources()["enabletest"] == nil {
			t.Fatal("expected source to be enabled")
		}
	})

	t.Run("Login", func(t *testing.T) {
		t.Skip("TODO: add coverage for oauth2")
	})
}

func panicerr(err error) {
	if err != nil {
		panic(err)
	}
}

func makeBlob(dir, content string) (string, error) {
	var t merkle.Tree
	if _, err := t.ReadFrom(strings.NewReader(content)); err != nil {
		return "", err
	}
	merkleroot := fmt.Sprintf("%x", t.Root())
	path := filepath.Join(dir, merkleroot)
	return merkleroot, ioutil.WriteFile(path, []byte(content), 0644)
}

func TestDaemon(t *testing.T) {
	dir, err := ioutil.TempDir("", "amber-test-store")
	panicerr(err)
	defer os.RemoveAll(dir)

	// TODO(raggi): make this a real package instead, but that's a lot more setup
	pkgContent := "very fake package"
	pkgBlobLength := int64(len(pkgContent))
	pkgBlob, err := makeBlob(dir, pkgContent)
	panicerr(err)
	root1, err := makeBlob(dir, "first blob")
	panicerr(err)

	repoDir, err := ioutil.TempDir("", "amber-test-repo")
	panicerr(err)
	defer os.RemoveAll(repoDir)

	// create and copy the package into the repo staging dir
	sTargetsDir := repoDir + "/staged/targets"
	panicerr(os.MkdirAll(sTargetsDir+"/foo", 0755))
	stagedPkg := sTargetsDir + "/foo/0"
	panicerr(os.Link(dir+"/"+pkgBlob, stagedPkg))

	panicerr(os.MkdirAll(repoDir+"/repository/blobs", 0755))
	panicerr(os.Link(dir+"/"+pkgBlob, repoDir+"/repository/blobs/"+pkgBlob))
	panicerr(os.Link(dir+"/"+root1, repoDir+"/repository/blobs/"+root1))

	// initialize the repo, adding the staged target
	repo, err := tuf.NewRepo(tuf.FileSystemStore(repoDir, nil), "sha512")
	panicerr(err)
	panicerr(repo.Init(true))
	_, err = repo.GenKey("root")
	panicerr(err)
	_, err = repo.GenKey("targets")
	panicerr(err)
	_, err = repo.GenKey("snapshot")
	panicerr(err)
	_, err = repo.GenKey("timestamp")
	panicerr(err)
	panicerr(repo.AddTarget("/foo/0", json.RawMessage(fmt.Sprintf(`{"merkle": %q}`, pkgBlob))))
	panicerr(repo.Snapshot(tuf.CompressionTypeNone))
	panicerr(repo.Timestamp())
	panicerr(repo.Commit())

	keys, err := repo.RootKeys()
	panicerr(err)
	rootKey := keys[0]

	server := httptest.NewServer(http.FileServer(http.Dir(repoDir + "/repository")))

	// XXX(raggi): cleanup disabled because networking bug!
	// defer server.Close()
	// // so that the httptest server can close:
	// defer http.DefaultTransport.(*http.Transport).CloseIdleConnections()

	dir, err = ioutil.TempDir("", "amber-test")
	panicerr(err)
	defer os.RemoveAll(dir)

	pkgsDir, err := ioutil.TempDir("", "amber-test-pkgs")
	panicerr(err)
	defer os.RemoveAll(pkgsDir)
	blobsDir, err := ioutil.TempDir("", "amber-test-blobs")
	panicerr(err)
	defer os.RemoveAll(blobsDir)

	d, err := NewDaemon(dir, pkgsDir, blobsDir)
	panicerr(err)

	err = d.AddSource(&amber.SourceConfig{
		Id:          "testing",
		RepoUrl:     server.URL,
		BlobRepoUrl: server.URL + "/blobs",
		// TODO(raggi): fix keyconfig
		RootKeys: []amber.KeyConfig{
			amber.KeyConfig{
				Type:  rootKey.Type,
				Value: rootKey.Value.Public.String(),
			},
		},
		StatusConfig: &amber.StatusConfig{Enabled: true},
	})
	panicerr(err)

	// TODO(raggi): add test for the update semantics
	d.Update()

	merkle, length, err := d.MerkleFor("foo", "0", "")
	if err != nil {
		t.Fatal(err)
	}
	if merkle != pkgBlob {
		t.Errorf("merkleFor: got %q, want %q", merkle, pkgBlob)
	}
	if length != int64(pkgBlobLength) {
		t.Errorf("merkleFor length: got %d, want %d", length, pkgBlobLength)
	}

	// TODO(raggi): add coverage for error propagatation cases
	var rootSeen string
	var errSeen error
	d.AddWatch(pkgBlob, func(root string, err error) {
		rootSeen = root
		errSeen = err
	})
	panicerr(d.GetPkg(pkgBlob, pkgBlobLength))

	d.Activated(pkgBlob)

	if rootSeen != pkgBlob {
		t.Errorf("activation: got %q, want %q", rootSeen, pkgBlob)
	}
	panicerr(errSeen)

	c, err := ioutil.ReadFile(pkgsDir + "/" + pkgBlob)
	panicerr(err)
	if got := string(c); got != pkgContent {
		t.Errorf("getpkg: got %q, want %q", got, pkgContent)
	}

	panicerr(d.GetBlob(root1))

	c, err = ioutil.ReadFile(blobsDir + "/" + root1)
	panicerr(err)
	if got, want := string(c), "first blob"; got != want {
		t.Errorf("getblob: got %q, want %q", got, want)
	}
}
