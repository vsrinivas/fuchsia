// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file implements a Go equivalent of:
// https://fuchsia.googlesource.com/fuchsia/+/HEAD/zircon/system/ulib/trace-provider/provider_impl.h
// https://fuchsia.googlesource.com/fuchsia/+/HEAD/zircon/system/ulib/trace-provider/provider_impl.cc

//go:build tracing
// +build tracing

// Package provider implements a trace provider service. The trace
// manager connects to trace providers to collect trace records from
// running programs.
package provider

import (
	"bytes"
	"context"
	"fmt"
	"syscall/zx"
	"syscall/zx/fdio"
	"syscall/zx/fidl"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/netstack/tracing/trace"
	"go.fuchsia.dev/fuchsia/src/lib/component"
	syslog "go.fuchsia.dev/fuchsia/src/lib/syslog/go"

	fidlprovider "fidl/fuchsia/tracing/provider"
)

const tag = "trace-provider"

const registryServicePath = "/svc/fuchsia.tracing.provider.Registry"

var _ fidlprovider.ProviderWithCtx = (*providerImpl)(nil)

type providerImpl struct {
	session *session
}

func (p *providerImpl) Initialize(_ fidl.Context, config fidlprovider.ProviderConfig) error {
	if err := p.session.initializeEngine(config.BufferingMode, config.Buffer, config.Fifo, config.Categories); err != nil {
		_ = syslog.ErrorTf(tag, "Initialize failed: %s", err)
	}
	return nil
}

func (p *providerImpl) Start(_ fidl.Context, options fidlprovider.StartOptions) error {
	if err := p.session.startEngine(options.BufferDisposition, options.AdditionalCategories); err != nil {
		_ = syslog.ErrorTf(tag, "Start failed: %s", err)
	}
	return nil
}

func (p *providerImpl) Stop(fidl.Context) error {
	if err := p.session.stopEngine(); err != nil {
		_ = syslog.ErrorTf(tag, "Stop failed: %s", err)
	}
	return nil
}

func (p *providerImpl) Terminate(fidl.Context) error {
	if err := p.session.terminateEngine(); err != nil {
		_ = syslog.ErrorTf(tag, "Terminate failed: %s", err)
	}
	return nil
}

func Create() error {
	registryReq, registryInterface, err := fidlprovider.NewRegistryWithCtxInterfaceRequest()
	if err != nil {
		return fmt.Errorf("failed to make a registry request/interface channel: %w", err)
	}
	if err := fdio.ServiceConnect(registryServicePath, zx.Handle(registryReq.Channel)); err != nil {
		return fmt.Errorf("failed to connect to %s: %w", registryServicePath, err)
	}

	providerReq, providerInterface, err := fidlprovider.NewProviderWithCtxInterfaceRequest()
	if err != nil {
		return fmt.Errorf("failed to make a provider request/interface channel: %w", err)
	}

	processName, err := getCurrentProcessName()
	if err != nil {
		return fmt.Errorf("failed to get the current process name: %w", err)
	}
	status, started, err := registryInterface.RegisterProviderSynchronously(context.Background(), *providerInterface, trace.GetCurrentProcessKoid(), processName)
	if err != nil {
		return fmt.Errorf("failed to register this provider interface: %w", err)
	}
	if zx.Status(status) != zx.ErrOk {
		return &zx.Error{
			Status: zx.Status(status),
			Text:   "fuchsia.tracing.provider/RegisterProviderSynchronously",
		}
	}
	if started {
		_ = syslog.InfoTf(tag, "tracing has already started. we won't start recording until the next Start()")
	}

	stub := fidlprovider.ProviderWithCtxStub{Impl: &providerImpl{session: newSession()}}
	go component.Serve(context.Background(), &stub, providerReq.Channel, component.ServeOptions{
		OnError: func(err error) {
			_ = syslog.WarnTf(tag, "%s", err)
		},
	})

	_ = syslog.InfoTf(tag, "Provider Created")
	return nil
}

func getCurrentProcessName() (string, error) {
	name := make([]byte, zx.ZX_MAX_NAME_LEN)
	if err := zx.ProcHandle.GetProperty(zx.PropName, name); err != nil {
		return "", err
	}
	n := bytes.IndexByte(name, 0)
	if n == -1 {
		n = len(name)
	}
	return string(name[:n]), nil
}
