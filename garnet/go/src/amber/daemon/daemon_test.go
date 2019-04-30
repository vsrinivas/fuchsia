// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package daemon

import (
	"crypto/rand"
	"encoding/json"
	"fmt"
	"io"
	"io/ioutil"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"fidl/fuchsia/amber"
	"fidl/fuchsia/pkg"

	"amber/source"

	"fuchsia.googlesource.com/merkle"
	"fuchsia.googlesource.com/pm/repo"
)

func TestSources(t *testing.T) {
	store, err := ioutil.TempDir("", "amber-test")
	if err != nil {
		panic(err)
	}
	defer os.RemoveAll(store)

	d, err := NewDaemon(store, "", "", "", nil)
	if err != nil {
		panic(err)
	}

	t.Run("Add", func(t *testing.T) {
		if err := d.AddSource(&amber.SourceConfig{
			Id:      "addtest",
			RepoUrl: "http://localhost/addtest",
			RootKeys: []amber.KeyConfig{
				{
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
				{
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
				{
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
				{
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
	store, err := ioutil.TempDir("", "amber-test-store")
	panicerr(err)
	defer os.RemoveAll(store)

	// TODO(raggi): make this a real package instead, but that's a lot more setup
	pkgContent := "very fake package"
	pkgBlobLength := int64(len(pkgContent))
	pkgBlob, err := makeBlob(store, pkgContent)
	panicerr(err)
	root1, err := makeBlob(store, "first blob")
	panicerr(err)

	repoDir, err := ioutil.TempDir("", "amber-test-repo")
	panicerr(err)
	defer os.RemoveAll(repoDir)

	// initialize the repo, adding the staged target
	repo, err := repo.New(repoDir)
	panicerr(err)
	panicerr(repo.Init())
	panicerr(repo.GenKeys())

	mf, err := os.Open(store + "/" + pkgBlob)
	panicerr(err)
	defer mf.Close()
	panicerr(repo.AddPackage("foo/0", mf))

	for _, blob := range []string{pkgBlob, root1} {
		b, err := os.Open(store + "/" + blob)
		panicerr(err)
		_, _, err = repo.AddBlob(blob, b)
		b.Close()
		panicerr(err)
	}

	panicerr(repo.CommitUpdates(false))

	keys, err := repo.RootKeys()
	panicerr(err)
	rootKey := keys[0]

	server := httptest.NewServer(http.FileServer(http.Dir(repoDir + "/repository")))

	// XXX(raggi): cleanup disabled because networking bug!
	// defer server.Close()
	// // so that the httptest server can close:
	// defer http.DefaultTransport.(*http.Transport).CloseIdleConnections()

	store, err = ioutil.TempDir("", "amber-test")
	panicerr(err)
	defer os.RemoveAll(store)

	pkgsDir, err := ioutil.TempDir("", "amber-test-pkgs")
	panicerr(err)
	defer os.RemoveAll(pkgsDir)
	blobsDir, err := ioutil.TempDir("", "amber-test-blobs")
	panicerr(err)
	defer os.RemoveAll(blobsDir)
	pkgNeedsDir, err := ioutil.TempDir("", "amber-test-pkgneeds")
	panicerr(err)
	defer os.RemoveAll(pkgNeedsDir)

	d, err := NewDaemon(store, pkgsDir, blobsDir, pkgNeedsDir, nil)
	panicerr(err)

	err = d.AddSource(&amber.SourceConfig{
		Id:          "testing",
		RepoUrl:     server.URL,
		BlobRepoUrl: server.URL + "/blobs",
		// TODO(raggi): fix keyconfig
		RootKeys: []amber.KeyConfig{
			{
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

	os.MkdirAll(filepath.Join(pkgNeedsDir, pkgBlob), 0755)
	panicerr(ioutil.WriteFile(filepath.Join(pkgNeedsDir, pkgBlob, root1), []byte{}, 0644))

	panicerr(d.GetPkg(pkgBlob, pkgBlobLength))

	c, err := ioutil.ReadFile(pkgsDir + "/" + pkgBlob)
	panicerr(err)
	if got := string(c); got != pkgContent {
		t.Errorf("getpkg: got %q, want %q", got, pkgContent)
	}

	c, err = ioutil.ReadFile(blobsDir + "/" + root1)
	panicerr(err)
	if got, want := string(c), "first blob"; got != want {
		t.Errorf("getblob: got %q, want %q", got, want)
	}
}

func TestOpenRepository(t *testing.T) {
	store, err := ioutil.TempDir("", "amber-test-store")
	panicerr(err)
	defer os.RemoveAll(store)

	// TODO(raggi): make this a real package instead, but that's a lot more setup
	pkgContent := "very fake package"
	pkgBlob, err := makeBlob(store, pkgContent)
	panicerr(err)
	root1, err := makeBlob(store, "first blob")
	panicerr(err)

	repoDir, err := ioutil.TempDir("", "amber-test-repo")
	panicerr(err)
	defer os.RemoveAll(repoDir)

	// initialize the repo, adding the staged target
	repo, err := repo.New(repoDir)
	panicerr(err)
	panicerr(repo.Init())
	panicerr(repo.GenKeys())

	mf, err := os.Open(store + "/" + pkgBlob)
	panicerr(err)
	defer mf.Close()
	panicerr(repo.AddPackage("foo/0", mf))

	for _, blob := range []string{pkgBlob, root1} {
		b, err := os.Open(store + "/" + blob)
		panicerr(err)
		_, _, err = repo.AddBlob(blob, b)
		b.Close()
		panicerr(err)
	}

	panicerr(repo.CommitUpdates(false))

	keys, err := repo.RootKeys()
	panicerr(err)
	rootKey := keys[0]

	server := httptest.NewServer(http.FileServer(http.Dir(repoDir + "/repository")))

	// XXX(raggi): cleanup disabled because networking bug!
	// defer server.Close()
	// // so that the httptest server can close:
	// defer http.DefaultTransport.(*http.Transport).CloseIdleConnections()

	keyConfig := &pkg.RepositoryKeyConfig{}
	keyConfig.SetEd25519Key(([]byte)(rootKey.Value.Public))

	r, err := source.OpenRepository(&pkg.RepositoryConfig{
		RepoUrl:        "fuchsia-pkg://testing",
		RepoUrlPresent: true,
		Mirrors: []pkg.MirrorConfig{
			{
				MirrorUrl:        server.URL,
				MirrorUrlPresent: true,
			},
		},
		MirrorsPresent: true,
		// TODO(raggi): fix keyconfig
		RootKeys:        []pkg.RepositoryKeyConfig{*keyConfig},
		RootKeysPresent: true,
	})
	panicerr(err)

	err = r.Update()
	panicerr(err)

	c, err := ioutil.ReadFile(r.LocalStoreDir() + "/tuf.json")
	panicerr(err)

	// Quick parse TUF store JSON to look at the signature on root.json
	type keyMeta struct {
		Keyid string
	}
	type rootMeta struct {
		Signatures []keyMeta
	}
	type tuf struct {
		Root rootMeta `json:"root.json"`
	}

	var meta tuf
	err = json.Unmarshal(c, &meta)
	panicerr(err)

	if rootKey.ID() != meta.Root.Signatures[0].Keyid {
		t.Fatalf("wrong signature in root.json; want key %s, got %s", rootKey.ID(), meta.Root.Signatures[0].Keyid)
	}
}

func TestDaemonWithEncryption(t *testing.T) {
	store, err := ioutil.TempDir("", "amber-test-store")
	panicerr(err)
	defer os.RemoveAll(store)

	// TODO(raggi): make this a real package instead, but that's a lot more setup
	pkgContent := "very fake package"
	pkgBlobLength := int64(len(pkgContent))
	pkgBlob, err := makeBlob(store, pkgContent)
	panicerr(err)
	root1, err := makeBlob(store, "first blob")
	panicerr(err)

	repoDir, err := ioutil.TempDir("", "amber-test-repo")
	panicerr(err)
	defer os.RemoveAll(repoDir)

	// initialize the repo, adding the staged target
	repo, err := repo.New(repoDir)
	panicerr(err)
	panicerr(repo.Init())
	panicerr(repo.GenKeys())

	// Create a blob encryption key
	var key [32]byte
	_, err = io.ReadFull(rand.Reader, key[:])
	panicerr(err)
	keyPath := store + "/crypt.key"
	err = ioutil.WriteFile(keyPath, key[:], 0600)
	panicerr(err)
	panicerr(repo.EncryptWith(keyPath))

	mf, err := os.Open(store + "/" + pkgBlob)
	panicerr(err)
	defer mf.Close()
	panicerr(repo.AddPackage("foo/0", mf))

	for _, blob := range []string{pkgBlob, root1} {
		b, err := os.Open(store + "/" + blob)
		panicerr(err)
		_, _, err = repo.AddBlob(blob, b)
		b.Close()
		panicerr(err)
	}

	panicerr(repo.CommitUpdates(false))

	keys, err := repo.RootKeys()
	panicerr(err)
	rootKey := keys[0]

	server := httptest.NewServer(http.FileServer(http.Dir(repoDir + "/repository")))

	// XXX(raggi): cleanup disabled because networking bug!
	// defer server.Close()
	// // so that the httptest server can close:
	// defer http.DefaultTransport.(*http.Transport).CloseIdleConnections()

	store, err = ioutil.TempDir("", "amber-test")
	panicerr(err)
	defer os.RemoveAll(store)

	pkgsDir, err := ioutil.TempDir("", "amber-test-pkgs")
	panicerr(err)
	defer os.RemoveAll(pkgsDir)
	blobsDir, err := ioutil.TempDir("", "amber-test-blobs")
	panicerr(err)
	defer os.RemoveAll(blobsDir)
	pkgNeedsDir, err := ioutil.TempDir("", "amber-test-pkgneeds")
	panicerr(err)
	defer os.RemoveAll(pkgNeedsDir)

	d, err := NewDaemon(store, pkgsDir, blobsDir, pkgNeedsDir, nil)
	panicerr(err)

	err = d.AddSource(&amber.SourceConfig{
		Id:          "testing",
		RepoUrl:     server.URL,
		BlobRepoUrl: server.URL + "/blobs",
		// TODO(raggi): fix keyconfig
		RootKeys: []amber.KeyConfig{
			{
				Type:  rootKey.Type,
				Value: rootKey.Value.Public.String(),
			},
		},
		BlobKey: &amber.BlobEncryptionKey{
			Data: key,
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

	os.MkdirAll(filepath.Join(pkgNeedsDir, pkgBlob), 0755)
	panicerr(ioutil.WriteFile(filepath.Join(pkgNeedsDir, pkgBlob, root1), []byte{}, 0644))

	panicerr(d.GetPkg(pkgBlob, pkgBlobLength))

	c, err := ioutil.ReadFile(pkgsDir + "/" + pkgBlob)
	panicerr(err)
	if got := string(c); got != pkgContent {
		t.Errorf("getpkg: got %q, want %q", got, pkgContent)
	}

	c, err = ioutil.ReadFile(blobsDir + "/" + root1)
	panicerr(err)
	if got, want := string(c), "first blob"; got != want {
		t.Errorf("getblob: got %q, want %q", got, want)
	}
}
