// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package rpc

import (
	"context"
	"fmt"
	"reflect"
	"syscall"
	"syscall/zx"
	"testing"
	"time"
	"unsafe"

	"fidl/fuchsia/io"

	"go.fuchsia.dev/fuchsia/src/lib/thinfs/fs"
)

type dummyFs struct {
	rootDir fs.Directory
}

func (d *dummyFs) Blockcount() int64           { return 0 }
func (d *dummyFs) Blocksize() int64            { return 0 }
func (d *dummyFs) Size() int64                 { return 0 }
func (d *dummyFs) Close() error                { return nil }
func (d *dummyFs) RootDirectory() fs.Directory { return d.rootDir }
func (d *dummyFs) Type() string                { return "dummy" }
func (d *dummyFs) FreeSize() int64             { return 0 }

func TestCookies(t *testing.T) {
	c1, c2, err := zx.NewChannel(0)
	if err != nil {
		t.Fatalf("failed to create channel: %v", err)
	}
	defer c1.Close()
	defer c2.Close()

	vfs, err := NewServer(&dummyFs{rootDir: nil}, c2)
	if err != nil {
		t.Fatalf("failed to create server: %v", err)
	}
	defer vfs.fs.Close()

	if len(vfs.dirs) != 1 {
		t.Fatalf("Unexpected number of directories. Want %d, got %d", 1, len(vfs.dirs))
	}

	for _, dir := range vfs.dirs {
		res, h, err := dir.GetToken(nil)
		if err != nil {
			t.Fatalf("GetToken(nil) failed: %v", err)
		}
		if zx.Status(res) != zx.ErrOk {
			t.Fatalf("GetToken(nil) returned wrong value. Want %v, got %v", zx.ErrOk, zx.Status(res))
		}

		dir.setCookie(32)
		if dir.getCookie(h) != 32 {
			t.Fatalf("Wrong Cookie retrieved. Want %d, got %d", 32, dir.getCookie(h))
		}
	}
}

type dummyDirectory struct {
	dirents []fs.Dirent
}

func (d *dummyDirectory) Close() error                                         { return nil }
func (d *dummyDirectory) Touch(lastAccess, lastModified time.Time) error       { return nil }
func (d *dummyDirectory) Dup() (fs.Directory, error)                           { return d, nil }
func (d *dummyDirectory) Read() ([]fs.Dirent, error)                           { return d.dirents, nil }
func (d *dummyDirectory) Rename(dstparent fs.Directory, src, dst string) error { return nil }
func (d *dummyDirectory) Sync() error                                          { return nil }
func (d *dummyDirectory) Unlink(target string) error                           { return nil }
func (d *dummyDirectory) Stat() (int64, time.Time, time.Time, error) {
	return 0, time.Unix(0, 0), time.Unix(0, 0), nil
}
func (d *dummyDirectory) Open(name string, flags fs.OpenFlags) (fs.File, fs.Directory, *fs.Remote, error) {
	return nil, nil, nil, nil
}

type fileDirEnt string

func (d fileDirEnt) GetType() fs.FileType { return fs.FileTypeRegularFile }
func (d fileDirEnt) GetIno() uint64       { return io.InoUnknown }
func (d fileDirEnt) GetName() string      { return string(d) }

func parseDirectoryEntryNames(t *testing.T, bytes []byte) []string {
	const direntNameSizeOffset = int(unsafe.Offsetof(syscall.Dirent{}.Size))

	names := []string{}

	for len(bytes) != 0 {
		nameSize := int(uint8(bytes[direntNameSizeOffset]))
		name := string(bytes[direntSize : direntSize+nameSize])

		names = append(names, name)

		bytes = bytes[direntSize+nameSize:]
	}

	return names
}

func readDirectoryEntryNames(t *testing.T, proxy *io.DirectoryWithCtxInterface) []string {
	res, err := proxy.Rewind(context.Background())
	if err != nil {
		t.Fatalf("Rewind() failed: %v", err)
	}
	if zx.Status(res) != zx.ErrOk {
		t.Fatalf("Rewind() returned wrong value. Want %v, got %v", zx.ErrOk, zx.Status(res))
	}

	names := []string{}

	for {
		// Use an arbitrary yet small buffer size to avoid having to use too
		// many entries for testing chunking edge cases.
		res, bytes, err := proxy.ReadDirents(context.Background(), 256)
		if err != nil {
			t.Fatalf("ReadDirents() failed: %v", err)
		}
		if zx.Status(res) != zx.ErrOk {
			t.Fatalf("ReadDirents() returned wrong value. Want %v, got %v", zx.ErrOk, zx.Status(res))
		}

		if len(bytes) == 0 {
			break
		}

		names = append(names, parseDirectoryEntryNames(t, bytes)...)
	}

	return names
}

func TestReadDirents(t *testing.T) {
	c1, c2, err := zx.NewChannel(0)
	if err != nil {
		t.Fatalf("failed to create channel: %v", err)
	}
	defer c1.Close()
	defer c2.Close()

	proxy := &io.DirectoryWithCtxInterface{Channel: c1}

	rootDirectory := &dummyDirectory{
		dirents: []fs.Dirent{},
	}
	vfs, err := NewServer(&dummyFs{rootDir: rootDirectory}, c2)
	if err != nil {
		t.Fatalf("failed to create server: %v", err)
	}
	defer vfs.fs.Close()

	expectedNames := []string{}

	for i := 0; i < 50; i++ {
		actualNames := readDirectoryEntryNames(t, proxy)
		if !reflect.DeepEqual(actualNames, expectedNames) {
			t.Errorf("Readdir() returned incorrect entries. got %v, want %v", actualNames, expectedNames)
		}

		name := fmt.Sprintf("entry%v", i)
		expectedNames = append(expectedNames, name)
		rootDirectory.dirents = append(rootDirectory.dirents, fileDirEnt(name))
	}
}

func TestFailingToSendOnOpenEventDoesNotCloseParentDir(t *testing.T) {
	c11, c12, err := zx.NewChannel(0)
	if err != nil {
		t.Fatalf("failed to create channel: %v", err)
	}
	defer c11.Close()
	defer c12.Close()

	proxy := &io.DirectoryWithCtxInterface{Channel: c11}

	rootDirectory := &dummyDirectory{
		dirents: []fs.Dirent{},
	}
	vfs, err := NewServer(&dummyFs{rootDir: rootDirectory}, c12)
	if err != nil {
		t.Fatalf("failed to create server: %v", err)
	}
	defer vfs.fs.Close()

	c21, c22, err := zx.NewChannel(0)
	if err != nil {
		t.Fatalf("failed to create channel: %v", err)
	}
	c21.Close()
	defer c22.Close()

	nodeReq := io.NodeWithCtxInterfaceRequest{Channel: c22}
	err = proxy.Open(context.Background(), io.OpenFlagDescribe, 0, "", nodeReq)
	if err != nil {
		t.Fatalf("failed to open child node: %v", err)
	}

	_, err = proxy.Describe(context.Background())
	if err != nil {
		t.Fatalf("failed to describe parent node: %v", err)
	}
}
