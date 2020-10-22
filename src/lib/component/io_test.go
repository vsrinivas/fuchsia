// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package component_test

import (
	"context"
	"syscall/zx"
	"syscall/zx/zxwait"
	"testing"

	"go.fuchsia.dev/fuchsia/src/lib/component"

	"fidl/fuchsia/io"
)

var _ component.Directory = (*mockDirImpl)(nil)

type mockDirImpl struct{}

func (*mockDirImpl) Get(string) (component.Node, bool) {
	return nil, false
}

func (*mockDirImpl) ForEach(func(string, component.Node)) {
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
	if _, err := zxwait.Wait(*proxy.Channel.Handle(), zx.SignalChannelPeerClosed, 0); err != nil {
		t.Fatalf("zxwait.Wait(_, zx.SignalChannelPeerClosed, 0) = %s", err)
	}
}
