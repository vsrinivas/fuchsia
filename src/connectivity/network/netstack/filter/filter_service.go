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
			go component.Serve(ctx, &stub, c, component.ServeOptions{
				OnError: func(err error) {
					_ = syslog.WarnTf(tag, "%s", err)
				},
			})
			return nil
		},
	)
}

func (fi *filterImpl) EnableInterface(_ fidl.Context, id uint64) (filter.FilterEnableInterfaceResult, error) {
	return fi.filter.enableInterface(tcpip.NICID(id)), nil
}

func (fi *filterImpl) DisableInterface(_ fidl.Context, id uint64) (filter.FilterDisableInterfaceResult, error) {
	return fi.filter.disableInterface(tcpip.NICID(id)), nil
}

func (fi *filterImpl) GetRules(fidl.Context) (filter.FilterGetRulesResult, error) {
	return fi.filter.getRules(), nil
}

func (fi *filterImpl) UpdateRules(_ fidl.Context, nrs []filter.Rule, generation uint32) (filter.FilterUpdateRulesResult, error) {
	return fi.filter.updateRules(nrs, generation), nil
}

func (fi *filterImpl) GetNatRules(fidl.Context) (filter.FilterGetNatRulesResult, error) {
	return fi.filter.getNATRules(), nil
}

func (fi *filterImpl) UpdateNatRules(_ fidl.Context, natRules []filter.Nat, generation uint32) (filter.FilterUpdateNatRulesResult, error) {
	return fi.filter.updateNATRules(natRules, generation), nil
}

func (*filterImpl) GetRdrRules(fidl.Context) (filter.FilterGetRdrRulesResult, error) {
	return filter.FilterGetRdrRulesResultWithResponse(filter.FilterGetRdrRulesResponse{}), nil
}

func (*filterImpl) UpdateRdrRules(fidl.Context, []filter.Rdr, uint32) (filter.FilterUpdateRdrRulesResult, error) {
	return filter.FilterUpdateRdrRulesResultWithErr(filter.FilterUpdateRdrRulesErrorNotSupported), nil
}
