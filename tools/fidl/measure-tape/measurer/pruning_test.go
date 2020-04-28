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
		var body block
		body.emitAddNumBytes("1")
		body.emitInvoke(calleeID, "value")
		caller = newMethod(callerID, &body)
	}
	{
		callee = newMethod(calleeID, nil)
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
		variantBlock.emitInvoke(calleeID, "value")

		var body block
		body.emitAddNumBytes("1")
		body.emitSelectVariant("value", fidlcommon.MustReadName("fidl/TargetType"), map[string]*block{
			"member": &variantBlock,
		})

		caller = newMethod(callerID, &body)
	}
	{
		callee = newMethod(calleeID, nil)
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
		body.emitAddNumBytes("1")
		body.emitInvoke(twoID, "value")
		one = newMethod(oneID, &body)
	}
	{
		var body block
		body.emitInvoke(threeID, "value")
		two = newMethod(twoID, &body)
	}
	{
		three = newMethod(threeID, nil)
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
