// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package measurer

import (
	"testing"

	fidlcommon "fidl/compiler/backend/common"
)

func TestPruningCallerCallingEmptyCallee(t *testing.T) {
	// caller -> callee (empty)
	var (
		callerID       = methodID{targetType: fidlcommon.MustReadName("fidl/Caller")}
		calleeID       = methodID{targetType: fidlcommon.MustReadName("fidl/Callee")}
		caller, callee *method
	)
	{
		expr := exprLocal("value", kStruct, false)
		var body block
		body.emitAddNumBytes(exprNum(1))
		body.emitInvoke(calleeID, expr)
		caller = newMethod(callerID, expr, &body)
	}
	{
		expr := exprLocal("value", kStruct, false)
		callee = newMethod(calleeID, expr, nil)
	}

	allMethods := map[methodID]*method{
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
		callerID       = methodID{targetType: fidlcommon.MustReadName("fidl/Caller")}
		calleeID       = methodID{targetType: fidlcommon.MustReadName("fidl/Callee")}
		caller, callee *method
	)
	{
		var variantBlock block
		variantBlock.emitInvoke(calleeID, exprLocal("value", kStruct, false))

		expr := exprLocal("value", kStruct, false)
		var body block
		body.emitAddNumBytes(exprNum(1))
		body.emitSelectVariant(exprNum(42), fidlcommon.MustReadName("fidl/TargetType"), map[string]*block{
			"member": &variantBlock,
		})

		caller = newMethod(callerID, expr, &body)
	}
	{
		expr := exprLocal("value", kStruct, false)
		callee = newMethod(calleeID, expr, nil)
	}

	allMethods := map[methodID]*method{
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
		oneID           = methodID{targetType: fidlcommon.MustReadName("fidl/One")}
		twoID           = methodID{targetType: fidlcommon.MustReadName("fidl/Two")}
		threeID         = methodID{targetType: fidlcommon.MustReadName("fidl/Three")}
		one, two, three *method
	)
	{
		var body block
		body.emitAddNumBytes(exprNum(1))
		body.emitInvoke(twoID, nil)
		one = newMethod(oneID, nil, &body)
	}
	{
		var body block
		body.emitInvoke(threeID, nil)
		two = newMethod(twoID, nil, &body)
	}
	{
		three = newMethod(threeID, nil, nil)
	}

	allMethods := map[methodID]*method{
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
