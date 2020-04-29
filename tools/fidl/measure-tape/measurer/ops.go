// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package measurer

import (
	"sort"

	fidlcommon "fidl/compiler/backend/common"
)

type statementKind int

const (
	_ statementKind = iota

	addNumBytes
	addNumHandles
	iterate
	invoke
	guard
	selectVariant
	maxOut
	declareMaxOrdinal
	setMaxOrdinal
)

// statement describes an operation that needs to be done in order to calculate
// the size of a FIDL value. Statements are high level operations, describing
// steps which can be taken within a measuring tape. (Speficially, they are not
// meant to be general purpose operations like one would find in general
// purpose programming languages' IR).
//
// A statement can be one of:
//
// * AddNumBytes(E): add E to the num bytes counter.
//
// * AddNumHandles(E): add E to the num handles counter.
//
// * Iterate(L, E, B): iterate over E, and run block B for each element. Each
//   element is referred to by L within block B.
//
// * Invoke(Id, E): invoke method identified as Id with value E.
//
// * Guard(M, B): run block B if member M is present.
//
// * SelectVariant(E, map[variant]B): run block B depending on the selected
//   variant of E. E must be a union.
//
// * MaxOut: max out both bytes and handles counters.
//
// * DeclareMaxOrdinal: declare a local meant to hold the max ordinal value.
//
// * SetMaxOrdinal(E): update the max ordinal local to E.
type statement struct {
	kind       statementKind
	id         methodID
	args       []Expression
	body       *block
	targetType fidlcommon.Name
	variants   map[string]*block

	deleted bool
}

type methodKind int

const (
	_ methodKind = iota

	measure
	measureOutOfLine
	measureHandles
)

type methodID struct {
	kind       methodKind
	targetType fidlcommon.Name
}

type byTargetTypeThenKind []methodID

var _ sort.Interface = byTargetTypeThenKind(nil)

func (s byTargetTypeThenKind) Less(i, j int) bool {
	lhs, rhs := s[i], s[j]
	if lhs.targetType.FullyQualifiedName() < rhs.targetType.FullyQualifiedName() {
		return true
	} else if lhs.targetType == rhs.targetType {
		return lhs.kind < rhs.kind
	}
	return false
}

func (s byTargetTypeThenKind) Len() int {
	return len(s)
}

func (s byTargetTypeThenKind) Swap(i, j int) {
	s[j], s[i] = s[i], s[j]
}

func (mt *MeasuringTape) methodIDOf(kind methodKind) methodID {
	mt.assertOnlyStructUnionTable()

	return methodID{
		kind:       kind,
		targetType: mt.name,
	}
}

type block struct {
	stmts []statement
}

func (b *block) emitAddNumBytes(expr Expression) {
	b.stmts = append(b.stmts, statement{
		kind: addNumBytes,
		args: []Expression{expr},
	})
}

func (b *block) emitAddNumHandles(expr Expression) {
	b.stmts = append(b.stmts, statement{
		kind: addNumHandles,
		args: []Expression{expr},
	})
}

func (b *block) emitInvoke(id methodID, expr Expression) {
	b.stmts = append(b.stmts, statement{
		kind: invoke,
		id:   id,
		args: []Expression{expr},
	})
}

func (b *block) emitIterate(local Expression, value Expression, body *block) {
	b.stmts = append(b.stmts, statement{
		kind: iterate,
		args: []Expression{local, value},
		body: body,
	})
}

func (b *block) emitGuard(cond Expression, body *block) {
	b.stmts = append(b.stmts, statement{
		kind: guard,
		args: []Expression{cond},
		body: body,
	})
}

func (b *block) emitMaxOut() {
	b.stmts = append(b.stmts, statement{
		kind: maxOut,
	})
}

const unknownVariant = ""

func (b *block) emitSelectVariant(expr Expression, targetType fidlcommon.Name, variants map[string]*block) {
	b.stmts = append(b.stmts, statement{
		kind:       selectVariant,
		args:       []Expression{expr},
		targetType: targetType,
		variants:   variants,
	})
}

func (b *block) emitDeclareMaxOrdinal() Expression {
	local := exprLocal("max_ordinal", kPrimitive, false)
	b.stmts = append(b.stmts, statement{
		kind: declareMaxOrdinal,
		args: []Expression{local},
	})
	return local
}

func (b *block) emitSetMaxOrdinal(local Expression, ordinal int) {
	b.stmts = append(b.stmts, statement{
		kind: setMaxOrdinal,
		args: []Expression{local, exprNum(ordinal)},
	})
}

func (b *block) forAllStatements(fn func(stmt *statement)) {
	for i := 0; i < len(b.stmts); i++ {
		if b.stmts[i].deleted {
			continue
		}
		fn(&b.stmts[i])
	}
}

type method struct {
	id   methodID
	arg  Expression
	body *block
}

func newMethod(id methodID, expr Expression, body *block) *method {
	return &method{
		id:   id,
		arg:  expr,
		body: body,
	}
}

// forAllStatements traverses all statements of a method but makes no guarantees
// as to the order in which the traversal is perfomed.
func (m *method) forAllStatements(fn func(stmt *statement)) {
	var (
		b             *block
		blocksToVisit = []*block{m.body}
	)
	for len(blocksToVisit) != 0 {
		b, blocksToVisit = blocksToVisit[0], blocksToVisit[1:]
		if b == nil {
			continue
		}
		b.forAllStatements(func(stmt *statement) {
			if stmt.body != nil {
				blocksToVisit = append(blocksToVisit, stmt.body)
			}
			for _, body := range stmt.variants {
				blocksToVisit = append(blocksToVisit, body)
			}
			fn(stmt)
		})
	}
}
