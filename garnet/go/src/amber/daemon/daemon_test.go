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

func tempPkgFs() source.PkgfsDir {
	pkgfspath, err := ioutil.TempDir("", "amber-test-pkgfs")
	panicerr(err)
	pkgfs := source.PkgfsDir{pkgfspath}
	os.MkdirAll(pkgfs.PkgInstallDir(), 0700)
	os.MkdirAll(pkgfs.BlobInstallDir(), 0700)
	os.MkdirAll(pkgfs.PkgNeedsDir(), 0700)
	os.MkdirAll(pkgfs.VersionsDir(), 0700)
	return pkgfs
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

	pkgfs := tempPkgFs()
	defer os.RemoveAll(pkgfs.RootDir)
	pkgsDir := pkgfs.PkgInstallDir()
	blobsDir := pkgfs.BlobInstallDir()
	pkgNeedsDir := pkgfs.PkgNeedsDir()

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
	}, pkgfs)
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

	os.MkdirAll(filepath.Join(pkgNeedsDir, pkgBlob), 0755)
	panicerr(ioutil.WriteFile(filepath.Join(pkgNeedsDir, pkgBlob, root1), []byte{}, 0644))
	panicerr(os.MkdirAll(filepath.Join(pkgfs.VersionsDir(), merkle), 0700))

	result, _, err := r.GetUpdateComplete("foo", nil, nil)
	panicerr(err)
	if result != pkgBlob {
		t.Errorf("GetUpdateComplete: got %q, want %q", result, pkgBlob)
	}

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

	pkgfs := tempPkgFs()
	defer os.RemoveAll(pkgfs.RootDir)
	pkgsDir := pkgfs.PkgInstallDir()
	blobsDir := pkgfs.BlobInstallDir()
	pkgNeedsDir := pkgfs.PkgNeedsDir()

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
	}, pkgfs)
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

	os.MkdirAll(filepath.Join(pkgNeedsDir, pkgBlob), 0755)
	panicerr(ioutil.WriteFile(filepath.Join(pkgNeedsDir, pkgBlob, root1), []byte{}, 0644))
	panicerr(os.MkdirAll(filepath.Join(pkgfs.VersionsDir(), merkle), 0700))

	result, _, err := r.GetUpdateComplete("foo", nil, nil)
	panicerr(err)
	if result != pkgBlob {
		t.Errorf("GetUpdateComplete: got %q, want %q", result, pkgBlob)
	}

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
