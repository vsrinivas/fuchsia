// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package filter

import (
	"syscall/zx"
	"syscall/zx/fidl"

	"app/context"
	"syslog"

	"fidl/fuchsia/net/filter"
)

type filterImpl struct {
	filter *Filter
}

var filterService filter.FilterService

func AddOutgoingService(ctx *context.Context, f *Filter) error {
	ctx.OutgoingService.AddService(
		filter.FilterName,
		&filter.FilterWithCtxStub{Impl: &filterImpl{filter: f}},
		func(s fidl.Stub, c zx.Channel) error {
			_, err := filterService.BindingSet.Add(s, c, nil)
			return err
		},
	)
	return nil
}

func (fi *filterImpl) Enable(_ fidl.Context, trueOrFalse bool) (filter.Status, error) {
	fi.filter.Enable(trueOrFalse)
	return 0, nil
}

func (fi *filterImpl) IsEnabled(fidl.Context) (bool, error) {
	return fi.filter.IsEnabled(), nil
}

func (fi *filterImpl) GetRules(fidl.Context) ([]filter.Rule, uint32, filter.Status, error) {
	fi.filter.rulesetMain.RLock()
	nrs, err := fromRules(fi.filter.rulesetMain.v)
	generation := fi.filter.rulesetMain.generation
	fi.filter.rulesetMain.RUnlock()
	if err != nil {
		syslog.Errorf("GetRules: %s", err)
		return []filter.Rule{}, 0, filter.StatusErrInternal, nil
	}
	return nrs, generation, filter.StatusOk, nil
}

func (fi *filterImpl) UpdateRules(_ fidl.Context, nrs []filter.Rule, generation uint32) (filter.Status, error) {
	// toRules validates rules during the conversion.
	rs, err := toRules(nrs)
	if err != nil {
		syslog.Errorf("UpdateRules: %s", err)
		return filter.StatusErrBadRule, nil
	}
	fi.filter.rulesetMain.Lock()
	defer fi.filter.rulesetMain.Unlock()
	if generation != fi.filter.rulesetMain.generation {
		return filter.StatusErrGenerationMismatch, nil
	}
	fi.filter.rulesetMain.v = rs
	fi.filter.rulesetMain.generation = generation + 1
	return filter.StatusOk, nil
}

func (fi *filterImpl) GetNatRules(fidl.Context) ([]filter.Nat, uint32, filter.Status, error) {
	fi.filter.rulesetNAT.RLock()
	nns, err := fromNATs(fi.filter.rulesetNAT.v)
	generation := fi.filter.rulesetNAT.generation
	fi.filter.rulesetNAT.RUnlock()
	if err != nil {
		syslog.Errorf("GetNATRules: %s", err)
		return []filter.Nat{}, 0, filter.StatusErrInternal, nil
	}
	return nns, generation, filter.StatusOk, nil
}

func (fi *filterImpl) UpdateNatRules(_ fidl.Context, nns []filter.Nat, generation uint32) (filter.Status, error) {
	ns, err := toNATs(nns)
	if err != nil {
		syslog.Errorf("UpdateNatRules: %s", err)
		return filter.StatusErrBadRule, nil
	}
	fi.filter.rulesetNAT.Lock()
	defer fi.filter.rulesetNAT.Unlock()
	if generation != fi.filter.rulesetNAT.generation {
		return filter.StatusErrGenerationMismatch, nil
	}
	fi.filter.rulesetNAT.v = ns
	fi.filter.rulesetNAT.generation = generation + 1
	return filter.StatusOk, nil
}

func (fi *filterImpl) GetRdrRules(fidl.Context) ([]filter.Rdr, uint32, filter.Status, error) {
	fi.filter.rulesetRDR.RLock()
	nrs, err := fromRDRs(fi.filter.rulesetRDR.v)
	generation := fi.filter.rulesetRDR.generation
	fi.filter.rulesetRDR.RUnlock()
	if err != nil {
		syslog.Errorf("GetRdrRules: %s", err)
		return []filter.Rdr{}, 0, filter.StatusErrInternal, nil
	}
	return nrs, generation, filter.StatusOk, nil
}

func (fi *filterImpl) UpdateRdrRules(_ fidl.Context, nrs []filter.Rdr, generation uint32) (filter.Status, error) {
	// toRDRs validates rules during the conversion.
	rs, err := toRDRs(nrs)
	if err != nil {
		syslog.Errorf("UpdateRdrRules: %s", err)
		return filter.StatusErrBadRule, nil
	}
	fi.filter.rulesetRDR.Lock()
	defer fi.filter.rulesetRDR.Unlock()
	if generation != fi.filter.rulesetRDR.generation {
		return filter.StatusErrGenerationMismatch, nil
	}
	fi.filter.rulesetRDR.v = rs
	fi.filter.rulesetRDR.generation = generation + 1
	return 0, nil
}
