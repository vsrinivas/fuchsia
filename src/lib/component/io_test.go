// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain

package component_test

import (
	"context"
	"errors"
	"syscall/zx"
	"testing"

	"go.fuchsia.dev/fuchsia/src/lib/component"

	"fidl/fuchsia/io"

	"github.com/google/go-cmp/cmp"
)

var _ component.Directory = (*mockDirImpl)(nil)

type mockDirImpl struct{}

func (*mockDirImpl) Get(string) (component.Node, bool) {
	return nil, false
}

func (*mockDirImpl) ForEach(func(string, component.Node) error) error {
	return nil
}

func TestHandleClosedOnOpenFailure(t *testing.T) {
	dir := component.DirectoryWrapper{
		Directory: &mockDirImpl{},
	}
	req, proxy, err := io.NewNodeWithCtxInterfaceRequest()
	if err != nil {
		t.Fatalf("io.NewNodeWithCtxInterfaceRequest() = %s", err)
	}
	defer func() {
		if err := proxy.Channel.Close(); err != nil {
			t.Fatalf("proxy.Channel.Close() = %s", err)
		}
	}()
	if err := dir.GetDirectory().Open(context.Background(), 0, 0, "non-existing node", req); err != nil {
		t.Fatalf("dir.GetDirecory.Open(...) = %s", err)
	}
	if status := zx.Sys_object_wait_one(*proxy.Channel.Handle(), zx.SignalChannelPeerClosed, 0, nil); status != zx.ErrOk {
		t.Fatalf("zx.Sys_object_wait_one(_, zx.SignalChannelPeerClosed, 0, _) = %s", status)
	}
}

var _ component.File = (*vmoFileImpl)(nil)

type vmoFileImpl struct {
	vmo zx.VMO
}

var errUnimplemented = errors.New("unimplemented")

type vmoReader struct {
	vmo zx.VMO
}

func (r *vmoReader) Read([]byte) (int, error) {
	return 0, errUnimplemented
}

func (r *vmoReader) ReadAt([]byte, int64) (int, error) {
	return 0, errUnimplemented
}

func (r *vmoReader) Seek(int64, int) (int64, error) {
	return 0, errUnimplemented
}

func (r *vmoReader) Close() error {
	return errUnimplemented
}

func (r *vmoReader) GetVMO() *zx.VMO {
	return &r.vmo
}

func (i *vmoFileImpl) GetReader() (component.Reader, uint64, error) {
	return &vmoReader{vmo: i.vmo}, 0, nil
}

func TestGetBackingMemory(t *testing.T) {
	content := []byte{
		1, 2, 3, 4, 5, 6,
	}

	vmo, err := zx.NewVMO(uint64(len(content)), 0)
	if err != nil {
		t.Fatalf("zx.NewVMO() = %s", err)
	}
	t.Cleanup(func() {
		if err := vmo.Close(); err != nil {
			t.Error(err)
		}
	})

	if err := vmo.Write(content, 0); err != nil {
		t.Fatalf("vmo.Write() = %s", err)
	}

	impl := vmoFileImpl{
		vmo: vmo,
	}
	file := component.FileWrapper{
		File: &impl,
	}
	f, err := file.GetFile()
	if err != nil {
		t.Fatalf("file.GetFile() = %s", err)
	}
	result, err := f.GetBackingMemory(context.Background(), 0)
	if err != nil {
		t.Fatalf("f.GetBackingMemory() = %s", err)
	}
	switch w := result.Which(); w {
	case io.FileGetBackingMemoryResultErr:
		t.Fatalf("file.GetBackingMemory() = %s", zx.Status(result.Err))
	case io.FileGetBackingMemoryResultResponse:
		vmo := result.Response.Vmo
		b := make([]byte, len(content))
		if err := vmo.Read(b, 0); err != nil {
			t.Fatalf("vmo.Read() = %s", err)
		}
		if diff := cmp.Diff(b, content); diff != "" {
			t.Errorf("vmo.Read() mismatch (-want +got):\n%s", diff)
		}
	default:
		t.Fatalf("file.GetBackingMemory().Which() = %d", w)
	}
}
