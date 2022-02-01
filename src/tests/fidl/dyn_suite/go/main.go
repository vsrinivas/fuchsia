// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain
// +build !build_with_native_toolchain

package main

import (
	"context"
	"fmt"
	"log"
	"syscall/zx"
	"syscall/zx/fidl"

	"go.fuchsia.dev/fuchsia/src/lib/component"

	"fidl/fidl/dynsuite"
)

type observer struct {
	client_end dynsuite.ObserverWithCtxInterface
}

func newObserver(client_end dynsuite.ObserverWithCtxInterface) *observer {
	obs := &observer{client_end}
	go obs.serveOnProgramPointEvents()
	return obs
}

func (obs *observer) serveOnProgramPointEvents() {
	for {
		program_point, err := obs.client_end.ExpectOnProgramPoint(context.Background())
		if err, ok := err.(*zx.Error); ok && err.Status == zx.ErrPeerClosed {
			return
		} else if err != nil {
			log.Println(err)
			obs.onError("ExpectOnProgramPoint", err)
			continue
		}
		obs.client_end.Observe(context.Background(), dynsuite.ObservationWithProgramPoint(program_point))
	}
}

func (obs *observer) onError(description string, err error) {
	obs.client_end.Observe(context.Background(), dynsuite.ObservationWithOnError(dynsuite.OnError{
		Context: description,
		Err:     int32(toZxStatus(err)),
	}))
}

func (obs *observer) onEnter(method dynsuite.Method) {
	obs.client_end.Observe(context.Background(), dynsuite.ObservationWithOnMethodInvocation(dynsuite.OnMethodInvocation{
		Method:      method,
		MethodPoint: dynsuite.MethodPointEnter,
	}))
}

func (obs *observer) onExit(method dynsuite.Method) {
	obs.client_end.Observe(context.Background(), dynsuite.ObservationWithOnMethodInvocation(dynsuite.OnMethodInvocation{
		Method:      method,
		MethodPoint: dynsuite.MethodPointExit,
	}))
}

func (obs *observer) onUnbind() {
	obs.client_end.Observe(context.Background(), dynsuite.ObservationWithOnUnbind(dynsuite.OnUnbind{}))
}

func (obs *observer) onComplete() {
	obs.client_end.Observe(context.Background(), dynsuite.ObservationWithOnComplete(dynsuite.OnComplete{}))
}

type entryImpl struct{}

var _ dynsuite.EntryWithCtx = (*entryImpl)(nil)

func (*entryImpl) StartServerTest(_ fidl.Context,
	server_end_to_test dynsuite.ServerTestWithCtxInterfaceRequest,
	client_end_to_observer dynsuite.ObserverWithCtxInterface) error {

	obs := newObserver(client_end_to_observer)

	// Observe: method invocation entry & exit.
	obs.onEnter(dynsuite.MethodStartServerTest)
	defer obs.onExit(dynsuite.MethodStartServerTest)

	// Start server test.
	stub := dynsuite.ServerTestWithCtxStub{
		Impl: &serverTestImpl{obs: obs},
	}
	go func() {
		defer func() {
			obs.onUnbind()
			obs.onComplete()
		}()
		component.Serve(context.Background(), &stub, server_end_to_test.Channel, component.ServeOptions{
			OnError: func(err error) {
				log.Println(err)
				obs.onError("dynsuite.ServerTest, OnError", err)
			},
		})
	}()

	return nil
}

type serverTestImpl struct {
	obs *observer
}

var _ dynsuite.ServerTestWithCtx = (*serverTestImpl)(nil)

func (t *serverTestImpl) OneWayInteractionNoPayload(_ fidl.Context) error {
	t.obs.onEnter(dynsuite.MethodOneWayInteractionNoPayload)
	defer t.obs.onExit(dynsuite.MethodOneWayInteractionNoPayload)
	return nil
}

func (*entryImpl) StartClientTest(_ fidl.Context,
	client_end_to_client_test dynsuite.ClientTestWithCtxInterface,
	client_end_to_observer dynsuite.ObserverWithCtxInterface) error {
	impl := &clientTestImpl{
		client_end: client_end_to_client_test,
		obs:        newObserver(client_end_to_observer),
	}
	go impl.serveOnPleaseDo()
	return nil
}

type clientTestImpl struct {
	client_end dynsuite.ClientTestWithCtxInterface
	obs        *observer
}

func (impl *clientTestImpl) serveOnPleaseDo() {
	for {
		action, err := impl.client_end.ExpectOnPleaseDo(context.Background())
		if err, ok := err.(*zx.Error); ok && err.Status == zx.ErrPeerClosed {
			return
		} else if err != nil {
			log.Println(err)
			impl.obs.onError("ExpectOnPleaseDo", err)
			continue
		}
		switch action.Which() {
		case dynsuite.ClientActionCloseChannel:
			if err := impl.client_end.Close(); err != nil {
				log.Println(err)
				impl.obs.onError("ClientAction.CloseChannel", err)
			}
		case dynsuite.ClientActionInvoke:
			switch action.Invoke {
			case dynsuite.MethodOneWayInteractionNoPayload:
				if err := impl.client_end.OneWayInteractionNoPayload(context.Background()); err != nil {
					log.Println(err)
					impl.obs.onError(fmt.Sprintf("ClientAction.Invoke(%s)", action.Invoke), err)
				}
			default:
				err := fmt.Errorf("unexpected %s", action.Invoke)
				log.Println(err)
				impl.obs.onError("ClientAction.Invoke", err)
			}
		}
	}
}

func toZxStatus(err error) zx.Status {
	if err, ok := err.(*zx.Error); ok {
		return err.Status
	}
	return zx.ErrInternal
}

func main() {
	log.SetFlags(log.Lshortfile)

	log.Println("Go dynsuite server: starting")
	ctx := component.NewContextFromStartupInfo()
	ctx.OutgoingService.AddService(
		dynsuite.EntryName,
		func(ctx context.Context, c zx.Channel) error {
			stub := dynsuite.EntryWithCtxStub{
				Impl: &entryImpl{},
			}
			go component.Serve(ctx, &stub, c, component.ServeOptions{
				OnError: func(err error) {
					log.Printf("dynsuite.Entry, OnError: %s\n", err)
				},
			})
			return nil
		},
	)
	log.Println("Go dynsuite server: ready")
	ctx.BindStartupHandle(context.Background())
}
