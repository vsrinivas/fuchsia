// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// +build !build_with_native_toolchain

package component_test

import (
	"context"
	"errors"
	"sync"
	"syscall/zx"
	"syscall/zx/fidl"
	"syscall/zx/zxwait"
	"testing"

	"go.fuchsia.dev/fuchsia/src/lib/component"

	"fidl/bindingstest"
)

var _ bindingstest.Test1WithCtx = (*test1Impl)(nil)

type test1Impl struct{}

func (t *test1Impl) Echo(_ fidl.Context, s *string) (*string, error) {
	return s, nil
}

func (t *test1Impl) NoResponse(fidl.Context) error {
	return nil
}

func (t *test1Impl) EmptyResponse(fidl.Context) error {
	return nil
}

func (t *test1Impl) EchoHandleRights(_ fidl.Context, h zx.Port) (uint32, error) {
	infoHandleBasic, err := h.Handle().GetInfoHandleBasic()
	if err != nil {
		return 0, err
	}
	return uint32(infoHandleBasic.Rights), h.Close()
}

func TestEmptyWriteErrors(t *testing.T) {
	var wg sync.WaitGroup

	ch, sh, err := zx.NewChannel(0)
	if err != nil {
		t.Fatal(err)
	}
	defer func() {
		if err := ch.Close(); err != nil {
			t.Error(err)
		}
		// Wait for the serving goroutine to exit, then confirm that ServeExclusive
		// closed the channel.
		wg.Wait()
		err := sh.Close()
		if err, ok := err.(*zx.Error); ok && err.Status == zx.ErrBadHandle {
			return
		}
		t.Errorf("got sh.Close() = %v, want %q", err, zx.ErrBadHandle)
	}()

	errChan := make(chan error, 1)

	wg.Add(1)
	go func() {
		defer wg.Done()
		component.ServeExclusive(context.Background(), &bindingstest.Test1WithCtxStub{
			Impl: &test1Impl{},
		}, sh, func(err error) {
			errChan <- err
		})
	}()

	if err := ch.Write(nil, nil, 0); err != nil {
		t.Fatal(err)
	}

	if _, err := zxwait.Wait(zx.Handle(ch), zx.SignalChannelPeerClosed, zx.TimensecInfinite); err != nil {
		t.Fatal(err)
	}

	close(errChan)
	if err := <-errChan; !errors.Is(err, fidl.ErrPayloadTooSmall) {
		t.Errorf("got ServeExclusive error = %v, want %q", err, fidl.ErrPayloadTooSmall)
	}
}

func TestEcho(t *testing.T) {
	var wg sync.WaitGroup

	ch, sh, err := zx.NewChannel(0)
	if err != nil {
		t.Fatal(err)
	}
	defer func() {
		// Expect the final test to have closed the client.
		func() {
			err := ch.Close()
			if err, ok := err.(*zx.Error); ok && err.Status == zx.ErrBadHandle {
				return
			}
			t.Errorf("got ch.Close() = %v, want %q", err, zx.ErrBadHandle)
		}()
		// Wait for the serving goroutine to exit, then confirm that ServeExclusive
		// closed the channel.
		wg.Wait()
		func() {
			err := sh.Close()
			if err, ok := err.(*zx.Error); ok && err.Status == zx.ErrBadHandle {
				return
			}
			t.Errorf("got sh.Close() = %v, want %q", err, zx.ErrBadHandle)
		}()
	}()

	wg.Add(1)
	go func() {
		defer wg.Done()
		component.ServeExclusive(context.Background(), &bindingstest.Test1WithCtxStub{
			Impl: &test1Impl{},
		}, sh, func(err error) {
			t.Error(err)
		})
	}()

	client := bindingstest.Test1WithCtxInterface{Channel: ch}

	t.Run("Basic", func(t *testing.T) {
		str := "Hello World!"
		r, err := client.Echo(context.Background(), &str)
		if err != nil {
			t.Fatal(err)
		}
		if r == nil {
			t.Fatalf("got Echo(%q) = nil", str)
		}
		if *r != str {
			t.Fatalf("got Echo(%q) = %q", str, *r)
		}
	})

	t.Run("NoResponse", func(t *testing.T) {
		if err := client.NoResponse(context.Background()); err != nil {
			t.Fatal(err)
		}
	})

	t.Run("EmptyResponse", func(t *testing.T) {
		if err := client.EmptyResponse(context.Background()); err != nil {
			t.Fatal(err)
		}
	})

	t.Run("Event", func(t *testing.T) {
		const str = "Surprise!"
		pxy := bindingstest.Test1EventProxy{Channel: sh}
		if err := pxy.Surprise(str); err != nil {
			t.Fatal(err)
		}
		s, err := client.ExpectSurprise(context.Background())
		if err != nil {
			t.Fatal(err)
		}
		if s != str {
			t.Fatalf("got Event = %q, want %q", str, s)
		}
	})

	t.Run("ReducingHandleRights", func(t *testing.T) {
		const expectedRights = zx.RightTransfer | zx.RightDuplicate | zx.RightRead
		port, err := zx.NewPort(0)
		if err != nil {
			t.Fatal(err)
		}
		defer func() {
			err := port.Close()
			// Ownership should have moved to the server, and it should have closed
			// the handle.
			if err, ok := err.(*zx.Error); ok && err.Status == zx.ErrBadHandle {
				return
			}
			t.Errorf("got port.Close() = %v, want %q", err, zx.ErrBadHandle)
		}()
		handleInfo, err := port.Handle().GetInfoHandleBasic()
		if err != nil {
			t.Fatal(err)
		}
		if !handleInfo.Rights.StrictSupersetOf(expectedRights) {
			t.Fatalf("got !(%b).StrictSupersetOf(%b)", handleInfo.Rights, expectedRights)
		}
		rights, err := client.EchoHandleRights(context.Background(), port)
		if err != nil {
			t.Fatal(err)
		}
		if rights := zx.Rights(rights); rights != expectedRights {
			t.Fatalf("got EchoHandleRights(...) = %b, want %b", rights, expectedRights)
		}
	})

	t.Run("MissingExpectedRightsSendingSide", func(t *testing.T) {
		// This test tests the sending side when there are missing expected rights.
		// The receiving side is harder to test because there needs to be a
		// mismatch of rights on the sending and receiving side.
		port, err := zx.NewPort(0)
		if err != nil {
			t.Fatal(err)
		}
		defer func() {
			err := port.Close()
			// This handle should have been consumed by the replace below.
			if err, ok := err.(*zx.Error); ok && err.Status == zx.ErrBadHandle {
				return
			}
			t.Errorf("got port.Close() = %v, want %q", err, zx.ErrBadHandle)
		}()
		const reducedRights = zx.RightTransfer | zx.RightDuplicate
		h, err := port.Handle().Replace(reducedRights)
		if err != nil {
			t.Fatal(err)
		}
		defer func() {
			err := h.Close()
			// Ownership should have moved to the server, and it should have closed
			// the handle.
			if err, ok := err.(*zx.Error); ok && err.Status == zx.ErrBadHandle {
				return
			}
			t.Errorf("got h.Close() = %v, want %q", err, zx.ErrBadHandle)
		}()
		handleInfo, err := h.GetInfoHandleBasic()
		if err != nil {
			t.Fatal(err)
		}
		if handleInfo.Rights != reducedRights {
			t.Fatalf("got Rights = %b, want %b", handleInfo.Rights, reducedRights)
		}
		{
			_, err := client.EchoHandleRights(context.Background(), zx.Port(h))
			if err, ok := err.(*zx.Error); ok && err.Status == zx.ErrInvalidArgs {
				return
			}
			t.Fatalf("got EchoHandleRights(reducedRights) = %v, want %q", err, zx.ErrInvalidArgs)
		}
	})
}

func TestServeExclusive_MagicNumberCheck(t *testing.T) {
	var wg sync.WaitGroup

	ch, sh, err := zx.NewChannel(0)
	if err != nil {
		t.Fatal(err)
	}
	defer func() {
		if err := ch.Close(); err != nil {
			t.Error(err)
		}
		// Wait for the serving goroutine to exit, then confirm that ServeExclusive
		// closed the channel.
		wg.Wait()
		err := sh.Close()
		if err, ok := err.(*zx.Error); ok && err.Status == zx.ErrBadHandle {
			return
		}
		t.Errorf("got sh.Close() = %v, want %q", err, zx.ErrBadHandle)
	}()

	errChan := make(chan error, 1)

	wg.Add(1)
	go func() {
		defer wg.Done()
		component.ServeExclusive(context.Background(), &bindingstest.Test1WithCtxStub{
			Impl: &test1Impl{},
		}, sh, func(err error) {
			errChan <- err
		})
	}()

	// Send an event with an unknown magic number. The method ordinal and message
	// body can also be invalid but magic numbers are checked first.
	request := []byte{
		0, 0, 0, 0, // txid
		0, 0, 0, // flags
		0,                      // magic number
		0, 0, 0, 0, 0, 0, 0, 1, // method ordinal
		0, 0, 0, 0, 0, 0, 0, 0, // empty struct data
	}

	if err := ch.Write(request, nil, 0); err != nil {
		t.Fatal(err)
	}

	if _, err := zxwait.Wait(zx.Handle(ch), zx.SignalChannelPeerClosed, zx.TimensecInfinite); err != nil {
		t.Fatal(err)
	}

	close(errChan)
	if err := <-errChan; !errors.Is(err, fidl.ErrUnknownMagic) {
		t.Errorf("got ServeExclusive error = %v, want %q", err, fidl.ErrUnknownMagic)
	}
}
