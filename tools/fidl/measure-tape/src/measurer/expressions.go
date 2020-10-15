// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package measurer

import "fmt"

// Expression represents an expression. It can be one of:
//
// * N, i.e. a number
//
// * L, i.e. a local variable. It is kinded and can be one of struct, union,
//   table, or unknown which is required for array and vector traversals.
//
// * MEMBER_OF(V, M), i.e. accessing a member of an expression. For instance,
//   this is done differently for a struct member, union member, or table member
//   in the HLCPP bindings.
//
// * FIDL_ALIGN(V), i.e. FIDL aligning an expression.
//
// * LENGTH(V), i.e. the length of an expression. For instance, this is either
//   v.length() or c.size() in the HLCPP bindings depending on the kind
//   of the expression, std::string vs std::vector receiver.
//   Only meaningul on expressions of kind string or vector.
//
// * HAS_MEMBER(V, M), i.e. check if a member of an expression is present. This
//   is meaningful for a nullable struct member, or a table member.
//
// * MULT(V1, V1), i.e. multiplication of two expressions.
type Expression interface {
	// AssertKind asserts that value is one of the `kinds` given. On success,
	// returns the kind of this value.
	AssertKind(kinds ...TapeKind) TapeKind

	// Nullable returns wether this value is nullable.
	Nullable() bool

	// Fmt formats the value with the specified formatter.
	Fmt(ExpressionFormatter) string
}

// ExpressionFormatter formats an expression.
type ExpressionFormatter interface {
	CaseNum(num int) string
	CaseLocal(name string, kind TapeKind) string
	CaseMemberOf(expr Expression, member string, kind TapeKind, nullable bool) string
	CaseFidlAlign(expr Expression) string
	CaseLength(expr Expression) string
	CaseHasMember(expr Expression, member string) string
	CaseMult(lhs, rhs Expression) string
}

var _ Expression = (*exprImpl)(nil)

// expr represents a Expression as a union, discriminated by a expressionKind.
//
// Nothing about expr is exported, and their construction is also
// unexported.
type exprImpl struct {
	discriminator expressionKind

	num      int
	expr     Expression
	name     string
	member   string
	kind     TapeKind
	nullable bool
	lhs, rhs Expression
}

type expressionKind int

const (
	_ expressionKind = iota

	num
	local
	memberOf
	fidlAlign
	length
	hasMember
	mult
)

func (expr *exprImpl) AssertKind(kinds ...TapeKind) TapeKind {
	for _, kind := range kinds {
		if expr.kind == kind {
			return kind
		}
	}
	panic(fmt.Sprintf("expected %v, was %v", kinds, expr.kind))
}

func (expr *exprImpl) Nullable() bool {
	return expr.nullable
}

func (expr *exprImpl) Fmt(formatter ExpressionFormatter) string {
	switch expr.discriminator {
	case num:
		return formatter.CaseNum(expr.num)
	case local:
		return formatter.CaseLocal(expr.name, expr.kind)
	case memberOf:
		return formatter.CaseMemberOf(expr.expr, expr.member, expr.kind, expr.nullable)
	case fidlAlign:
		return formatter.CaseFidlAlign(expr.expr)
	case length:
		return formatter.CaseLength(expr.expr)
	case hasMember:
		return formatter.CaseHasMember(expr.expr, expr.member)
	case mult:
		return formatter.CaseMult(expr.lhs, expr.rhs)
	}
	panic(fmt.Sprintf("unexpected expressionKind %v", expr.discriminator))
}

func exprNum(n int) Expression {
	return &exprImpl{
		discriminator: num,
		num:           n,
	}
}

func exprLocal(name string, kind TapeKind, nullable bool) Expression {
	return &exprImpl{
		discriminator: local,
		name:          name,
		kind:          kind,
		nullable:      nullable,
	}
}

func exprMemberOf(expr Expression, member string, kind TapeKind, nullable bool) Expression {
	expr.AssertKind(Struct, Union, Table)
	return &exprImpl{
		discriminator: memberOf,
		expr:          expr,
		member:        member,
		kind:          kind,
		nullable:      nullable,
	}
}

func exprFidlAlign(expr Expression) Expression {
	return &exprImpl{
		discriminator: fidlAlign,
		expr:          expr,
	}
}

func exprLength(expr Expression) Expression {
	expr.AssertKind(Vector, Array, String)
	return &exprImpl{
		discriminator: length,
		expr:          expr,
	}
}

func exprHasMember(expr Expression, member string) Expression {
	return &exprImpl{
		discriminator: hasMember,
		expr:          expr,
		member:        member,
	}
}

func exprMult(lhs, rhs Expression) Expression {
	return &exprImpl{
		discriminator: mult,
		lhs:           lhs,
		rhs:           rhs,
	}
}
