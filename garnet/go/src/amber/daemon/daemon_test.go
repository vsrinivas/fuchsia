// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package daemon

import (
	"crypto/rand"
	"fmt"
	"io"
	"io/ioutil"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"fidl/fuchsia/pkg"

	"amber/source"

	"fuchsia.googlesource.com/merkle"
	"fuchsia.googlesource.com/pm/repo"
)

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

func TestOpenRepository(t *testing.T) {
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
	panicerr(repo.AddPackage("foo/0", mf, ""))

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

	merkle, length, err := r.MerkleFor("foo", "0", "")
	if err != nil {
		t.Fatal(err)
	}
	if merkle != pkgBlob {
		t.Errorf("merkleFor: got %q, want %q", merkle, pkgBlob)
	}
	if length != int64(pkgBlobLength) {
		t.Errorf("merkleFor length: got %d, want %d", length, pkgBlobLength)
	}
}

func TestOpenRepositoryWithEncryption(t *testing.T) {
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
	panicerr(repo.AddPackage("foo/0", mf, ""))

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

	blobKey := &pkg.RepositoryBlobKey{}
	blobKey.SetAesKey(key[:])

	r, err := source.OpenRepository(&pkg.RepositoryConfig{
		RepoUrl:        "fuchsia-pkg://testing",
		RepoUrlPresent: true,
		Mirrors: []pkg.MirrorConfig{
			{
				MirrorUrl:        server.URL,
				MirrorUrlPresent: true,
				BlobKey:          *blobKey,
				BlobKeyPresent:   true,
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

	merkle, length, err := r.MerkleFor("foo", "0", "")
	if err != nil {
		t.Fatal(err)
	}
	if merkle != pkgBlob {
		t.Errorf("merkleFor: got %q, want %q", merkle, pkgBlob)
	}
	if length != int64(pkgBlobLength) {
		t.Errorf("merkleFor length: got %d, want %d", length, pkgBlobLength)
	}
}
