// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package measurer

import (
	"fmt"
	"sort"

	fidlcommon "go.fuchsia.dev/fuchsia/garnet/go/src/fidl/compiler/backend/common"
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
type Statement struct {
	kind       statementKind
	id         MethodID
	args       []Expression
	body       *Block
	targetType fidlcommon.Name
	variants   map[string]LocalWithBlock

	deleted bool
}

type StatementFormatter interface {
	CaseMaxOut()
	CaseAddNumBytes(expr Expression)
	CaseAddNumHandles(expr Expression)
	CaseInvoke(id MethodID, expr Expression)
	CaseGuard(cond Expression, body *Block)
	CaseIterate(local, expr Expression, body *Block)
	CaseSelectVariant(expr Expression, targetType fidlcommon.Name, variants map[string]LocalWithBlock)
	CaseDeclareMaxOrdinal(local Expression)
	CaseSetMaxOrdinal(local, ordinal Expression)
}

func (stmt *Statement) Visit(formatter StatementFormatter) {
	switch stmt.kind {
	case addNumBytes:
		formatter.CaseAddNumBytes(stmt.args[0])
	case addNumHandles:
		formatter.CaseAddNumHandles(stmt.args[0])
	case iterate:
		formatter.CaseIterate(stmt.args[0], stmt.args[1], stmt.body)
	case invoke:
		formatter.CaseInvoke(stmt.id, stmt.args[0])
	case guard:
		formatter.CaseGuard(stmt.args[0], stmt.body)
	case selectVariant:
		formatter.CaseSelectVariant(stmt.args[0], stmt.targetType, stmt.variants)
	case maxOut:
		formatter.CaseMaxOut()
	case declareMaxOrdinal:
		formatter.CaseDeclareMaxOrdinal(stmt.args[0])
	case setMaxOrdinal:
		formatter.CaseSetMaxOrdinal(stmt.args[0], stmt.args[1])
	default:
		panic(fmt.Sprintf("unexpected statementKind %v", stmt.kind))
	}
}

type MethodKind int

const (
	_ MethodKind = iota

	Measure
	MeasureOutOfLine
	MeasureHandles
)

type MethodID struct {
	Kind       MethodKind
	TargetType fidlcommon.Name
}

type ByTargetTypeThenKind []MethodID

var _ sort.Interface = ByTargetTypeThenKind(nil)

func (s ByTargetTypeThenKind) Less(i, j int) bool {
	lhs, rhs := s[i], s[j]
	if lhs.TargetType.FullyQualifiedName() < rhs.TargetType.FullyQualifiedName() {
		return true
	} else if lhs.TargetType == rhs.TargetType {
		return lhs.Kind < rhs.Kind
	}
	return false
}

func (s ByTargetTypeThenKind) Len() int {
	return len(s)
}

func (s ByTargetTypeThenKind) Swap(i, j int) {
	s[j], s[i] = s[i], s[j]
}

func (mt *MeasuringTape) methodIDOf(kind MethodKind) MethodID {
	mt.assertOnlyStructUnionTable()

	return MethodID{
		Kind:       kind,
		TargetType: mt.name,
	}
}

type Block struct {
	stmts []Statement
}

func (b *Block) emitAddNumBytes(expr Expression) {
	b.stmts = append(b.stmts, Statement{
		kind: addNumBytes,
		args: []Expression{expr},
	})
}

func (b *Block) emitAddNumHandles(expr Expression) {
	b.stmts = append(b.stmts, Statement{
		kind: addNumHandles,
		args: []Expression{expr},
	})
}

func (b *Block) emitInvoke(id MethodID, expr Expression) {
	b.stmts = append(b.stmts, Statement{
		kind: invoke,
		id:   id,
		args: []Expression{expr},
	})
}

func (b *Block) emitIterate(local Expression, value Expression, body *Block) {
	b.stmts = append(b.stmts, Statement{
		kind: iterate,
		args: []Expression{local, value},
		body: body,
	})
}

func (b *Block) emitGuard(cond Expression, body *Block) {
	b.stmts = append(b.stmts, Statement{
		kind: guard,
		args: []Expression{cond},
		body: body,
	})
}

func (b *Block) emitMaxOut() {
	b.stmts = append(b.stmts, Statement{
		kind: maxOut,
	})
}

// UnknownVariant represents the unknown variant case in a select variant
// statement.
const UnknownVariant = ""

type LocalWithBlock struct {
	Local Expression
	Body  *Block
}

func (b *Block) emitSelectVariant(expr Expression, targetType fidlcommon.Name, variants map[string]LocalWithBlock) {
	b.stmts = append(b.stmts, Statement{
		kind:       selectVariant,
		args:       []Expression{expr},
		targetType: targetType,
		variants:   variants,
	})
}

func (b *Block) emitDeclareMaxOrdinal() Expression {
	local := exprLocal("max_ordinal", Primitive, false)
	b.stmts = append(b.stmts, Statement{
		kind: declareMaxOrdinal,
		args: []Expression{local},
	})
	return local
}

func (b *Block) emitSetMaxOrdinal(local Expression, ordinal int) {
	b.stmts = append(b.stmts, Statement{
		kind: setMaxOrdinal,
		args: []Expression{local, exprNum(ordinal)},
	})
}

func (b *Block) ForAllStatements(fn func(stmt *Statement)) {
	for i := 0; i < len(b.stmts); i++ {
		if b.stmts[i].deleted {
			continue
		}
		fn(&b.stmts[i])
	}
}

type Method struct {
	ID   MethodID
	Arg  Expression
	Body *Block
}

func newMethod(id MethodID, expr Expression, body *Block) *Method {
	return &Method{
		ID:   id,
		Arg:  expr,
		Body: body,
	}
}

// ForAllStatements traverses all statements of a method but makes no guarantees
// as to the order in which the traversal is perfomed.
func (m *Method) ForAllStatements(fn func(stmt *Statement)) {
	var (
		b             *Block
		blocksToVisit = []*Block{m.Body}
	)
	for len(blocksToVisit) != 0 {
		b, blocksToVisit = blocksToVisit[0], blocksToVisit[1:]
		if b == nil {
			continue
		}
		b.ForAllStatements(func(stmt *Statement) {
			if stmt.body != nil {
				blocksToVisit = append(blocksToVisit, stmt.body)
			}
			for _, localWithBlock := range stmt.variants {
				blocksToVisit = append(blocksToVisit, localWithBlock.Body)
			}
			fn(stmt)
		})
	}
}
