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
	"runtime"
	"strings"
	"syscall"
	"syscall/zx"
	"syscall/zx/fidl"
	zxio "syscall/zx/io"
	"testing"
	"thinfs/fs"
	"thinfs/zircon/rpc"

	"fidl/fuchsia/amber"

	"fuchsia.googlesource.com/merkle"
	"fuchsia.googlesource.com/pmd/amberer"
	"fuchsia.googlesource.com/pmd/pkgfs"
	tuf "github.com/flynn/go-tuf"
)

func TestSources(t *testing.T) {
	fmt.Printf("hi\n")
	store, err := ioutil.TempDir("", "amber-test")
	if err != nil {
		panic(err)
	}
	defer os.RemoveAll(store)

	d, err := NewDaemon(store, "", "", nil)
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

func initRepo(repoDir string, store string, blobs []string) *tuf.Repo {
	// create and copy the package into the repo staging dir
	sTargetsDir := repoDir + "/staged/targets"
	panicerr(os.MkdirAll(sTargetsDir+"/foo", 0755))
	stagedPkg := sTargetsDir + "/foo/0"
	pkgBlob := blobs[0]
	panicerr(os.Link(store+"/"+pkgBlob, stagedPkg))

	panicerr(os.MkdirAll(repoDir+"/repository/blobs", 0755))
	for _, blob := range blobs {
		panicerr(os.Link(store+"/"+blob, repoDir+"/repository/blobs/"+blob))
	}

	// create the repo
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
	return repo
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
	repo := initRepo(repoDir, store, []string{pkgBlob, root1})
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

	d, err := NewDaemon(store, pkgsDir, blobsDir, nil)
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

// Mock blobfs that always returns "no space".

type blobfsFile struct {
	pkgfs.UnsupportedFile
}

func (f blobfsFile) Truncate(size uint64) error {
	return fs.ErrNoSpace
}

var _ = fs.File(blobfsFile{})

type blobfsDirectory struct {
	pkgfs.UnsupportedDirectory
}

func (d blobfsDirectory) Open(name string, flags fs.OpenFlags) (fs.File, fs.Directory, *fs.Remote, error) {
	return blobfsFile{}, nil, nil, nil
}

var _ = fs.Directory(blobfsDirectory{})

type blobfsFilesystem struct {
	serveChannel zx.Channel
}

func (fs *blobfsFilesystem) Mount(path string) error {
	var err error
	err = os.MkdirAll(path, os.ModePerm)
	panicerr(err)
	parentFd, err := syscall.Open(path, syscall.O_ADMIN|syscall.O_DIRECTORY, 0777)
	panicerr(err)
	var rpcChan, mountChan zx.Channel
	rpcChan, mountChan, err = zx.NewChannel(0)
	panicerr(err)
	remote := zxio.DirectoryInterface(fidl.InterfaceRequest{Channel: mountChan})
	dirChan := zx.Channel(syscall.FDIOForFD(parentFd).Handles()[0])
	dir := zxio.DirectoryAdminInterface(fidl.InterfaceRequest{Channel: dirChan})
	panicerr(err)
	status, err := dir.Mount(remote)
	if zx.Status(status) != zx.ErrOk {
		panic(fmt.Sprintf("Mount error: %s", zx.Status(status)))
	}
	panicerr(err)
	fs.Serve(rpcChan)
	return nil
}

func (fs *blobfsFilesystem) Unmount() {
	// Just a test, let this be a no-op.
}

// Starts a Directory protocol RPC server on the given channel. Does not block.
func (fs *blobfsFilesystem) Serve(c zx.Channel) error {
	// rpc.NewServer takes ownership of the Handle and will close it on error.
	vfs, err := rpc.NewServer(fs, zx.Handle(c))
	if err != nil {
		return fmt.Errorf("vfs server creation: %s", err)
	}
	fs.serveChannel = c

	// TODO(raggi): serve has no quit/shutdown path.
	for i := runtime.NumCPU(); i > 0; i-- {
		go vfs.Serve()
	}
	return nil
}

func (fs *blobfsFilesystem) Blockcount() int64 {
	return 0
}

func (fs *blobfsFilesystem) Blocksize() int64 {
	return 0
}

func (fs *blobfsFilesystem) Size() int64 {
	return 0
}

func (fs *blobfsFilesystem) Close() error {
	fs.Unmount()
	return nil
}

func (fs *blobfsFilesystem) RootDirectory() fs.Directory {
	return blobfsDirectory{}
}

func (fs *blobfsFilesystem) Type() string {
	return "blobfs"
}

func (fs *blobfsFilesystem) FreeSize() int64 {
	return 0
}

func (fs *blobfsFilesystem) DevicePath() string {
	return ""
}

var _ = fs.FileSystem(&blobfsFilesystem{})

type eventsImpl struct{}

var _ = amber.Events(eventsImpl{})

func TestOutOfSpace(t *testing.T) {
	var err error

	// Fake blobfs that always returns fs.ErrNoSpace.
	blobfsRootPath, err := ioutil.TempDir("", "pkgfs-test-blobfs")
	defer os.RemoveAll(blobfsRootPath)
	panicerr(err)

	blobfsPath := filepath.Join(blobfsRootPath, "blobfs")
	var fs blobfsFilesystem
	fs.Mount(blobfsPath)
	defer fs.Unmount()

	// Set up pkgfs.

	indexPath, err := ioutil.TempDir("", "pkgfs-test-index")
	if err != nil {
		panic(err)
	}
	panicerr(err)
	defer os.RemoveAll(indexPath)

	pkgfs, err := pkgfs.New(indexPath, blobfsPath, amberer.NewAmberClient())
	panicerr(err)

	pkgfsDir, err := ioutil.TempDir("", "pkgfs-test-mount")
	panicerr(err)
	defer os.RemoveAll(pkgfsDir)

	go func() {
		if err := pkgfs.Mount(pkgfsDir); err != nil {
			panic(err)
		}
	}()
	defer pkgfs.Unmount()

	// Set up daemon.

	store, err := ioutil.TempDir("", "amber-test")
	panicerr(err)
	defer os.RemoveAll(store)

	installPkgDir := filepath.Join(pkgfsDir, "install/pkg")
	installBlobDir := filepath.Join(pkgfsDir, "install/blob")
	var evtSvc amber.EventsService
	d, err := NewDaemon(store, installPkgDir, installBlobDir, &evtSvc)
	panicerr(err)

	// Create a fake package and blob.

	// TODO(raggi): make this a real package instead, but that's a lot more setup
	pkgContent := "very fake package"
	pkgBlob, err := makeBlob(store, pkgContent)
	panicerr(err)
	root1Content := "first blob"
	root1Length := int64(len(root1Content))
	root1, err := makeBlob(store, root1Content)
	panicerr(err)

	// Add a source.

	repoDir, err := ioutil.TempDir("", "amber-test-repo")
	panicerr(err)
	defer os.RemoveAll(repoDir)
	repo := initRepo(repoDir, store, []string{pkgBlob, root1})
	keys, err := repo.RootKeys()
	panicerr(err)

	rootKey := keys[0]
	server := httptest.NewServer(http.FileServer(http.Dir(repoDir + "/repository")))

	// XXX(raggi): cleanup disabled because networking bug!
	// defer server.Close()
	// // so that the httptest server can close:
	// defer http.DefaultTransport.(*http.Transport).CloseIdleConnections()

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

	// Fetch into daemon and check that the event was signaled.
	req, evtIfc, err := amber.NewEventsInterfaceRequest()
	panicerr(err)
	_, err = evtSvc.Add(eventsImpl{}, req.ToChannel(), nil)
	panicerr(err)
	err = d.fetchInto(root1, root1Length, installBlobDir)
	if e, ok := err.(zx.Error); ok && e.Status == zx.ErrNoSpace {
		// Expected.
	} else {
		panic(fmt.Sprintf("fetchInto returned unexpected error %s", err))
	}
	err = evtIfc.ExpectOnOutOfSpace()
	panicerr(err)
}
