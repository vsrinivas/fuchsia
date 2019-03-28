// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package filter

import (
	"syscall/zx"

	"app/context"
	"syslog/logger"

	"fidl/fuchsia/net/filter"
)

type filterImpl struct {
	filter *Filter
}

var filterService filter.FilterService

func AddOutgoingService(ctx *context.Context, f *Filter) error {
	ctx.OutgoingService.AddService(filter.FilterName, func(c zx.Channel) error {
		_, err := filterService.Add(&filterImpl{filter: f}, c, nil)
		return err
	})
	return nil
}

func (fi *filterImpl) Enable(trueOrFalse bool) (filter.Status, error) {
	fi.filter.Enable(trueOrFalse)
	return 0, nil
}

func (fi *filterImpl) IsEnabled() (bool, error) {
	return fi.filter.IsEnabled(), nil
}

func (fi *filterImpl) GetRules() ([]filter.Rule, uint32, filter.Status, error) {
	fi.filter.rulesetMain.RLock()
	nrs, err := fromRules(fi.filter.rulesetMain.v)
	generation := fi.filter.rulesetMain.generation
	fi.filter.rulesetMain.RUnlock()
	if err != nil {
		// This error should not happen.
		logger.Errorf("GetRules error %v", err)
		return []filter.Rule{}, 0, filter.StatusErrInternal, nil
	}
	return nrs, generation, filter.StatusOk, nil
}

func (fi *filterImpl) UpdateRules(nrs []filter.Rule, generation uint32) (filter.Status, error) {
	rs, err := toRules(nrs)
	if err != nil {
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

func (fi *filterImpl) GetNatRules() ([]filter.Nat, uint32, filter.Status, error) {
	fi.filter.rulesetNAT.RLock()
	nns, err := fromNATs(fi.filter.rulesetNAT.v)
	generation := fi.filter.rulesetNAT.generation
	fi.filter.rulesetNAT.RUnlock()
	if err != nil {
		// This error should not happen.
		logger.Errorf("GetNATRules error %v", err)
		return []filter.Nat{}, 0, filter.StatusErrInternal, nil
	}
	return nns, generation, filter.StatusOk, nil
}

func (fi *filterImpl) UpdateNatRules(nns []filter.Nat, generation uint32) (filter.Status, error) {
	ns, err := toNATs(nns)
	if err != nil {
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

func (fi *filterImpl) GetRdrRules() ([]filter.Rdr, uint32, filter.Status, error) {
	fi.filter.rulesetRDR.RLock()
	nrs, err := fromRDRs(fi.filter.rulesetRDR.v)
	generation := fi.filter.rulesetRDR.generation
	fi.filter.rulesetRDR.RUnlock()
	if err != nil {
		// This error should not happen.
		logger.Errorf("GetRdrRules error %v", err)
		return []filter.Rdr{}, 0, filter.StatusErrInternal, nil
	}
	return nrs, generation, filter.StatusOk, nil
}

func (fi *filterImpl) UpdateRdrRules(nrs []filter.Rdr, generation uint32) (filter.Status, error) {
	rs, err := toRDRs(nrs)
	if err != nil {
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
