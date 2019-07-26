// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//TODO(PKG-710) Replace with Rust/isolated pkgfs when available

package install

import (
	"crypto/rand"
	"fmt"
	"io"
	"io/ioutil"
	"net"
	"net/http"
	"os"
	"path/filepath"
	"sync"
	"syscall/zx"
	"testing"
	"time"

	"fidl/fuchsia/pkg"

	"amber/source"

	"fuchsia.googlesource.com/pm/build"
	"fuchsia.googlesource.com/pm/repo"
)

func TestRepoRecoverFromPreinstalledBlobs(t *testing.T) {
	// Make the test package
	cfg := build.TestConfig()
	cfg.PkgName = randName(t.Name())
	defer os.RemoveAll(filepath.Dir(cfg.TempDir))
	build.BuildTestPackage(cfg)

	pkgManifestPath := filepath.Join(cfg.OutputDir, "package_manifest.json")
	blobs, err := cfg.BlobInfo()
	if err != nil {
		t.Fatal(err)
	}
	metaFar, blobs := blobs[0], blobs[1:]

	// Ensure pkgfs is in a good state (no previous test has written this test package)
	if _, err := os.Stat(filepath.Join("/pkgfs/versions", metaFar.Merkle.String())); err == nil {
		t.Fatal("test package already readable")
	}

	if _, err := os.Stat(filepath.Join("/pkgfs/needs/packages", metaFar.Merkle.String())); err == nil {
		t.Fatal("test package already has needs dir")
	}

	// Make the test repo, and start serving it over http
	repoDir, err := ioutil.TempDir("", "amber-install-test-repo")
	if err != nil {
		t.Fatal(err)
	}
	defer os.RemoveAll(repoDir)

	repo, err := repo.New(repoDir)
	if err != nil {
		t.Fatal(err)
	}
	if err := repo.Init(); err != nil {
		t.Fatal(err)
	}
	if err := repo.GenKeys(); err != nil {
		t.Fatal(err)
	}
	if _, err := repo.PublishManifest(pkgManifestPath); err != nil {
		t.Fatal(err)
	}
	if err := repo.CommitUpdates(false); err != nil {
		t.Fatal(err)
	}

	server, err := serveStaticRepo(t, filepath.Join(repoDir, "repository"))
	if err != nil {
		t.Fatal(err)
	}
	defer server.Close()

	// Open a new repository, and register this source
	keys, err := repo.RootKeys()
	if err != nil {
		t.Fatal(err)
	}

	rootKeys := []pkg.RepositoryKeyConfig{}
	for _, key := range keys {
		keyConfig := &pkg.RepositoryKeyConfig{}
		keyConfig.SetEd25519Key(([]byte)(key.Value.Public))
		rootKeys = append(rootKeys, *keyConfig)
	}

	r, err := source.OpenRepository(&pkg.RepositoryConfig{
		RepoUrl:        "fuchsia-pkg://installtest",
		RepoUrlPresent: true,
		Mirrors: []pkg.MirrorConfig{
			{
				MirrorUrl:        server.baseURL,
				MirrorUrlPresent: true,
			},
		},
		MirrorsPresent:  true,
		RootKeys:        rootKeys,
		RootKeysPresent: true,
	}, source.PkgfsDir{"/pkgfs"})

	if err != nil {
		t.Fatal(err)
	}

	// Install the package's blobs before pkgfs knows about the package
	for _, blob := range blobs {
		// Install only those blobs that are missing
		installBlob(t, blob.SourcePath, blob.Merkle.String(), int64(blob.Size), true)
	}

	// Write the meta FAR as a blob (so the package installation flow is not triggered)
	installBlob(t, metaFar.SourcePath, metaFar.Merkle.String(), int64(metaFar.Size), false)

	// Now attempt to install the package
	noMerklePin := ""
	actualMerkle, zxStatus, err := r.GetUpdateComplete(cfg.PkgName, &cfg.PkgVersion, &noMerklePin)
	if err != nil {
		t.Fatal(err)
	}
	if zxStatus != zx.ErrOk {
		t.Fatal(zxStatus)
	}
	if metaFar.Merkle.String() != actualMerkle {
		t.Fatalf("invalid merkle: expected %q, got %q", metaFar.Merkle, actualMerkle)
	}

	// Ensure the package now exists and is readable
	pkgDir := filepath.Join("/pkgfs/versions", metaFar.Merkle.String())
	if _, err := os.Stat(pkgDir); err != nil {
		t.Fatal(err)
	}
}
func TestRepoRecoverFromInterruptedInstall(t *testing.T) {
	// Make the test package
	cfg := build.TestConfig()
	cfg.PkgName = randName(t.Name())
	defer os.RemoveAll(filepath.Dir(cfg.TempDir))
	build.BuildTestPackage(cfg)

	pkgManifestPath := filepath.Join(cfg.OutputDir, "package_manifest.json")
	blobs, err := cfg.BlobInfo()
	if err != nil {
		t.Fatal(err)
	}
	metaFar, blobs := blobs[0], blobs[1:]

	// Ensure pkgfs is in a good state (no previous test has written this test package)
	if _, err := os.Stat(filepath.Join("/pkgfs/versions", metaFar.Merkle.String())); err == nil {
		t.Fatal("test package already readable")
	}

	if _, err := os.Stat(filepath.Join("/pkgfs/needs/packages", metaFar.Merkle.String())); err == nil {
		t.Fatal("test package already has needs dir")
	}

	// Make the test repo, and start serving it over http
	repoDir, err := ioutil.TempDir("", "amber-install-test-repo")
	if err != nil {
		t.Fatal(err)
	}
	defer os.RemoveAll(repoDir)

	repo, err := repo.New(repoDir)
	if err != nil {
		t.Fatal(err)
	}
	if err := repo.Init(); err != nil {
		t.Fatal(err)
	}
	if err := repo.GenKeys(); err != nil {
		t.Fatal(err)
	}
	if _, err := repo.PublishManifest(pkgManifestPath); err != nil {
		t.Fatal(err)
	}
	if err := repo.CommitUpdates(false); err != nil {
		t.Fatal(err)
	}

	server, err := serveStaticRepo(t, filepath.Join(repoDir, "repository"))
	if err != nil {
		t.Fatal(err)
	}
	defer server.Close()

	// Open a new repository, and register this source
	keys, err := repo.RootKeys()
	if err != nil {
		t.Fatal(err)
	}

	rootKeys := []pkg.RepositoryKeyConfig{}
	for _, key := range keys {
		keyConfig := &pkg.RepositoryKeyConfig{}
		keyConfig.SetEd25519Key(([]byte)(key.Value.Public))
		rootKeys = append(rootKeys, *keyConfig)
	}

	r, err := source.OpenRepository(&pkg.RepositoryConfig{
		RepoUrl:        "fuchsia-pkg://installtest",
		RepoUrlPresent: true,
		Mirrors: []pkg.MirrorConfig{
			{
				MirrorUrl:        server.baseURL,
				MirrorUrlPresent: true,
			},
		},
		MirrorsPresent:  true,
		RootKeys:        rootKeys,
		RootKeysPresent: true,
	}, source.PkgfsDir{"/pkgfs"})

	if err != nil {
		t.Fatal(err)
	}

	// Write the meta FAR as a blob (so the package installation flow is not triggered)
	installBlob(t, metaFar.SourcePath, metaFar.Merkle.String(), int64(metaFar.Size), false)

	// Now attempt to install the package
	noMerklePin := ""
	actualMerkle, zxStatus, err := r.GetUpdateComplete(cfg.PkgName, &cfg.PkgVersion, &noMerklePin)
	if err != nil {
		t.Fatal(err)
	}
	if zxStatus != zx.ErrOk {
		t.Fatal(zxStatus)
	}
	if metaFar.Merkle.String() != actualMerkle {
		t.Fatalf("invalid merkle: expected %q, got %q", metaFar.Merkle, actualMerkle)
	}

	// Ensure the package now exists and is readable
	pkgDir := filepath.Join("/pkgfs/versions", metaFar.Merkle.String())
	if _, err := os.Stat(pkgDir); err != nil {
		t.Fatal(err)
	}
}

// staticHttpServer serves a static directory
type staticHttpServer struct {
	baseURL  string
	server   http.Server
	waitStop sync.WaitGroup
}

// Close stops the server, waiting for it to terminate
func (s *staticHttpServer) Close() {
	s.server.Close()
	s.waitStop.Wait()
}

// waitForUp waits for the server to respond to GET /
func (s *staticHttpServer) waitForUp() error {
	// without restructuring the runtime significantly, there's not a good way to
	// detect the server is up, so we try a few times and fail if there are no
	// successes.
	for i := 0; i < 10; i++ {
		_, err := http.Get(s.baseURL + "/")
		if err != nil {
			time.Sleep(time.Second / 10)
			continue
		}
		break
	}
	return nil
}

// serveStaticRepo serves a static directory over Http
func serveStaticRepo(t *testing.T, repoDir string) (*staticHttpServer, error) {
	port := getPort(t)
	listen := fmt.Sprintf("127.0.0.1:%d", port)

	s := &staticHttpServer{
		baseURL: fmt.Sprintf("http://%s", listen),
	}
	s.server.Addr = listen
	s.server.Handler = http.FileServer(http.Dir(repoDir))

	s.waitStop.Add(1)
	go func() {
		defer s.waitStop.Done()
		err := s.server.ListenAndServe()
		if err != nil && err != http.ErrServerClosed {
			t.Fatal(err)
		}
	}()

	err := s.waitForUp()
	if err != nil {
		s.Close()
		return nil, err
	}

	return s, nil
}

// get a free port, with a very small chance of race
func getPort(t *testing.T) int {
	t.Helper()
	s, err := net.ListenTCP("tcp", &net.TCPAddr{net.IPv4(127, 0, 0, 1), 0, ""})
	if err != nil {
		t.Fatal(err)
	}
	defer s.Close()
	return s.Addr().(*net.TCPAddr).Port
}

// readMerkle reads a hex encoded merkle root from path, or fails the test
func readMerkle(t *testing.T, path string) string {
	t.Helper()
	bytes, err := ioutil.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	return string(bytes)
}

// installBlob writes a blob to pkgfs through the blob install path
func installBlob(t *testing.T, path string, merkle string, length int64, allowExists bool) {
	t.Helper()
	src, err := os.Open(path)
	if err != nil {
		t.Fatal(err)
	}
	defer src.Close()

	dest, err := os.OpenFile(filepath.Join("/pkgfs/install/blob", merkle), os.O_CREATE|os.O_WRONLY, 0644)
	if err != nil {
		if allowExists && os.IsExist(err) {
			return
		}
		t.Fatal(err)
	}
	defer dest.Close()

	if err := dest.Truncate(length); err != nil {
		t.Fatal(err)
	}

	if _, err := io.Copy(dest, src); err != nil {
		t.Fatal(err)
	}
}

func randName(prefix string) string {
	b := make([]byte, 8)
	if _, err := io.ReadFull(rand.Reader, b); err != nil {
		panic(err)
	}
	return prefix + fmt.Sprintf("%x", b)
}
