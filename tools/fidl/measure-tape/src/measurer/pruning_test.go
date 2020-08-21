// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package measurer

import (
	"testing"

	fidlcommon "go.fuchsia.dev/fuchsia/garnet/go/src/fidl/compiler/backend/common"
)

func TestPruningCallerCallingEmptyCallee(t *testing.T) {
	// caller -> callee (empty)
	var (
		callerID       = MethodID{TargetType: fidlcommon.MustReadName("fidl/Caller")}
		calleeID       = MethodID{TargetType: fidlcommon.MustReadName("fidl/Callee")}
		caller, callee *Method
	)
	{
		expr := exprLocal("value", Struct, false)
		var body Block
		body.emitAddNumBytes(exprNum(1))
		body.emitInvoke(calleeID, expr)
		caller = newMethod(callerID, expr, &body)
	}
	{
		expr := exprLocal("value", Struct, false)
		callee = newMethod(calleeID, expr, nil)
	}

	allMethods := map[MethodID]*Method{
		callerID: caller,
		calleeID: callee,
	}

	pruneEmptyMethods(allMethods)

	if len(allMethods) != 1 {
		t.Fatalf("should have pruned the graph")
	}
	if _, ok := allMethods[callerID]; !ok {
		t.Fatalf("should not have removed the caller")
	}
}

func TestPruningCallerCallingEmptyCalleeThroughSelectVariant(t *testing.T) {
	// caller -> callee (empty)
	var (
		callerID       = MethodID{TargetType: fidlcommon.MustReadName("fidl/Caller")}
		calleeID       = MethodID{TargetType: fidlcommon.MustReadName("fidl/Callee")}
		caller, callee *Method
	)
	{
		var variantBlock Block
		variantBlock.emitInvoke(calleeID, exprLocal("value", Struct, false))

		expr := exprLocal("value", Struct, false)
		var body Block
		body.emitAddNumBytes(exprNum(1))
		body.emitSelectVariant(nil, fidlcommon.MustReadName("fidl/TargetType"), map[string]LocalWithBlock{
			"member": {Body: &variantBlock},
		})

		caller = newMethod(callerID, expr, &body)
	}
	{
		expr := exprLocal("value", Struct, false)
		callee = newMethod(calleeID, expr, nil)
		var body Block
		body.emitAddNumBytes(exprNum(1))
		body.emitInvoke(calleeID, nil)
		caller = newMethod(callerID, expr, &body)
	}
	{
		expr := exprLocal("value", Struct, false)
		callee = newMethod(calleeID, expr, nil)
	}

	allMethods := map[MethodID]*Method{
		callerID: caller,
		calleeID: callee,
	}

	pruneEmptyMethods(allMethods)

	if len(allMethods) != 1 {
		t.Fatalf("should have pruned the graph")
	}
	if _, ok := allMethods[callerID]; !ok {
		t.Fatalf("should not have removed the caller")
	}
}

func TestPruningOneCallingTwoCallingEmptyThree(t *testing.T) {
	// one -> two -> three (empty)
	var (
		oneID           = MethodID{TargetType: fidlcommon.MustReadName("fidl/One")}
		twoID           = MethodID{TargetType: fidlcommon.MustReadName("fidl/Two")}
		threeID         = MethodID{TargetType: fidlcommon.MustReadName("fidl/Three")}
		one, two, three *Method
	)
	{
		var body Block
		body.emitAddNumBytes(exprNum(1))
		body.emitInvoke(twoID, nil)
		one = newMethod(oneID, nil, &body)
	}
	{
		var body Block
		body.emitInvoke(threeID, nil)
		two = newMethod(twoID, nil, &body)
	}
	{
		three = newMethod(threeID, nil, nil)
	}

	allMethods := map[MethodID]*Method{
		oneID:   one,
		twoID:   two,
		threeID: three,
	}

	pruneEmptyMethods(allMethods)

	if len(allMethods) != 1 {
		t.Fatalf("should have pruned the graph")
	}
	if _, ok := allMethods[oneID]; !ok {
		t.Fatalf("should not have removed 'the one'")
	}
}
