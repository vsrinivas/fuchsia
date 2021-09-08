// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//go:build !build_with_native_toolchain
// +build !build_with_native_toolchain

package filter

import (
	"context"
	"syscall/zx"
	"syscall/zx/fidl"

	"go.fuchsia.dev/fuchsia/src/lib/component"
	syslog "go.fuchsia.dev/fuchsia/src/lib/syslog/go"

	"fidl/fuchsia/net/filter"

	"gvisor.dev/gvisor/pkg/tcpip"
)

type filterImpl struct {
	filter *Filter
}

func AddOutgoingService(ctx *component.Context, f *Filter) {
	stub := filter.FilterWithCtxStub{Impl: &filterImpl{filter: f}}
	ctx.OutgoingService.AddService(
		filter.FilterName,
		func(ctx context.Context, c zx.Channel) error {
			go component.ServeExclusive(ctx, &stub, c, func(err error) {
				_ = syslog.WarnTf(tag, "%s", err)
			})
			return nil
		},
	)
}

func (f *filterImpl) EnableInterface(_ fidl.Context, id uint64) (filter.Status, error) {
	return f.filter.EnableInterface(tcpip.NICID(id)), nil
}

func (f *filterImpl) DisableInterface(_ fidl.Context, id uint64) (filter.Status, error) {
	return f.filter.DisableInterface(tcpip.NICID(id)), nil
}

func (f *filterImpl) GetRules(fidl.Context) ([]filter.Rule, uint32, filter.Status, error) {
	rules, generation := f.filter.lastRules()
	return rules, generation, filter.StatusOk, nil
}

func (f *filterImpl) UpdateRules(_ fidl.Context, nrs []filter.Rule, generation uint32) (filter.Status, error) {
	return f.filter.updateRules(nrs, generation), nil
}

func (*filterImpl) GetNatRules(fidl.Context) ([]filter.Nat, uint32, filter.Status, error) {
	return nil, 1, filter.StatusOk, nil
}

func (*filterImpl) UpdateNatRules(fidl.Context, []filter.Nat, uint32) (filter.Status, error) {
	return filter.StatusErrNotSupported, nil
}

func (*filterImpl) GetRdrRules(fidl.Context) ([]filter.Rdr, uint32, filter.Status, error) {
	return nil, 1, filter.StatusOk, nil
}

func (*filterImpl) UpdateRdrRules(fidl.Context, []filter.Rdr, uint32) (filter.Status, error) {
	return filter.StatusErrNotSupported, nil
}
