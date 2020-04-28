// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package measurer

func removeInvokeAndCountRemainingStatements(m *method, id methodID) int {
	var count int
	m.forAllStatements(func(stmt *statement) {
		if stmt.kind == invoke && stmt.id == id {
			stmt.deleted = true
		} else {
			count++
		}
	})
	return count
}

// pruneEmptyMethods removes empty methods, and invocations to any removed
// empty method. If the removal of the invocation in turn causes the caller to
// become an empty method, we repeat until we reach a fixed point, and no
// empty method is left in the call graph.
//
// The implementation is very conservative, and for instance does not remove
// guard, select-variant, or iterate loops which have empty bodies. Because the
// expression which we guard, select-variant of, or iterate over would need to
// be pure (no side effect), it would be safer to increase the precisiohn of
// method pruning at the same time as introducing a predicate ensuring this
// is the case.
func pruneEmptyMethods(allMethods map[methodID]*method) {
	// boostrap
	// - creating a map of callee to all its callers
	// - recording all initially empty methods
	var (
		calledBy     = make(map[methodID][]methodID)
		emptyMethods []methodID
	)
	for _, m := range allMethods {
		var (
			caller  = m.id
			isEmpty = true
		)
		m.forAllStatements(func(stmt *statement) {
			if stmt.kind == invoke {
				callee := stmt.id
				calledBy[callee] = append(calledBy[callee], caller)
			}
			isEmpty = false
		})
		if isEmpty {
			emptyMethods = append(emptyMethods, caller)
		}
	}

	// The pruning algorithm starts from the leaves, and continues to remove
	// empty nodes, going up the tree. The algorithm stops going upwards from a
	// node if the node did not become empty even after removing its empty
	// children.
	//
	// Should we want to extend this algorithm to prune empty bodies (and not
	// just empty methods), we can replace the caller-callee from being at
	// the method level to being at the block-to-parent-block level.
	p := pruner{
		allMethods: allMethods,
		calledBy:   calledBy,
	}
	for _, emptyMethodID := range emptyMethods {
		p.prune(emptyMethodID)
	}
}

type pruner struct {
	allMethods map[methodID]*method
	calledBy   map[methodID][]methodID
}

func (p pruner) prune(methodID methodID) {
	delete(p.allMethods, methodID)
	for _, callerID := range p.calledBy[methodID] {
		if caller, ok := p.allMethods[callerID]; ok {
			if removeInvokeAndCountRemainingStatements(caller, methodID) == 0 {
				p.prune(caller.id)
			}
		}
	}
}
