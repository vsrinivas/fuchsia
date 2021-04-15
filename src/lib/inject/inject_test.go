// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package inject

import (
	"fmt"
	"reflect"
	"strings"
	"testing"
)

func TestRequirePtrToStruct(t *testing.T) {
	targets := []interface{}{
		int32(4),
		make(map[string]rune),
		struct{}{},
		[]bool{true},
	}
	for _, target := range targets {
		if _, err := Start(target); err == nil {
			t.Errorf("expected failure when starting: %T", target)
		} else if !strings.Contains(err.Error(), "must be pointer to struct") {
			t.Errorf("expected a different failure when starting: %s", err)
		}
	}
}

func TestEmptyStruct(t *testing.T) {
	target := &struct{}{}
	stopper, err := Start(target)
	if err != nil {
		t.Fatalf("when starting: %s", err)
	}
	checkStoppingOrder(t, stopper, []reflect.Type{
		reflect.TypeOf((*struct{})(nil)),
	})
	if err := stopper.Stop(); err != nil {
		t.Fatalf("when stopping: %s", err)
	}
}

type pointsToSelf struct {
	Self *pointsToSelf `inject:""`
}

func TestPointsToSelf(t *testing.T) {
	target := &pointsToSelf{}
	_, err := Start(target)
	if err == nil {
		t.Fatalf("expected failure when starting: %T", target)
	}
	if !strings.Contains(err.Error(), "*inject.pointsToSelf > *inject.pointsToSelf: cyclic dependency") {
		t.Fatalf("expected a different failure when starting: %s", err)
	}
}

type simpleChainOne struct {
	SkipMe       string
	SkipMeAsWell string
	Two          *simpleChainTwo `inject:""`
}

type simpleChainTwo struct {
	Three *simpleChainThree `inject:""`
}

type simpleChainThree struct {
	Here   string
	We     string
	Skip   string
	More   string
	Fields string
}

func TestSimpleChain(t *testing.T) {
	target := &simpleChainOne{}
	stopper, err := Start(target)
	if err != nil {
		t.Fatalf("when starting: %s", err)
	}
	checkStoppingOrder(t, stopper, []reflect.Type{
		reflect.TypeOf((*simpleChainOne)(nil)),
		reflect.TypeOf((*simpleChainTwo)(nil)),
		reflect.TypeOf((*simpleChainThree)(nil)),
	})
	if target.Two == nil {
		t.Fatalf("two is nil")
	}
	if target.Two.Three == nil {
		t.Fatalf("three is nil")
	}
	if err := stopper.Stop(); err != nil {
		t.Fatalf("when stopping: %s", err)
	}
}

type diamondTop struct {
	Left  *diamondLeft  `inject:""`
	Right *diamondRight `inject:""`
}

type diamondLeft struct {
	Bottom *diamondBottom `inject:""`
}

type diamondRight struct {
	Bottom *diamondBottom `inject:""`
}

type diamondBottom struct {
}

func TestDiamond(t *testing.T) {
	top := &diamondTop{}
	stopper, err := Start(top)
	if err != nil {
		t.Fatalf("when starting: %s", err)
	}
	checkStoppingOrder(t, stopper, []reflect.Type{
		reflect.TypeOf((*diamondTop)(nil)),
		reflect.TypeOf((*diamondLeft)(nil)),
		reflect.TypeOf((*diamondBottom)(nil)),
		reflect.TypeOf((*diamondRight)(nil)),
	})
	if top.Left == nil {
		t.Fatalf("top.left is nil")
	}
	if top.Right == nil {
		t.Fatalf("top.right is nil")
	}
	if top.Left.Bottom == nil {
		t.Fatalf("left.bottom is nil")
	}
	if top.Right.Bottom == nil {
		t.Fatalf("right.bottom is nil")
	}
	if top.Left.Bottom != top.Right.Bottom {
		t.Fatalf("left.bottom is not the same as right.bottom")
	}
	if err := stopper.Stop(); err != nil {
		t.Fatalf("when stopping: %s", err)
	}
}

type configurableStartAndStop struct {
	failToStart, failToStop   bool
	startCounter, stopCounter int
}

var _ Starter = (*configurableStartAndStop)(nil)
var _ Stopper = (*configurableStartAndStop)(nil)

func (module *configurableStartAndStop) Start() error {
	if module.failToStart {
		return fmt.Errorf("failed to start")
	}
	module.startCounter++
	return nil
}

func (module *configurableStartAndStop) Stop() error {
	if module.failToStop {
		return fmt.Errorf("failed to stop")
	}
	module.stopCounter++
	return nil
}

func TestStartStopSucceed(t *testing.T) {
	target := &configurableStartAndStop{}
	stopper, err := Start(target)
	if err != nil {
		t.Fatalf("when starting: %s", err)
	}
	if err := stopper.Stop(); err != nil {
		t.Fatalf("when stopping: %s", err)
	}
	if expected := 1; target.startCounter != expected {
		t.Fatalf("startCounter expected %d, found %d", expected, target.startCounter)
	}
	if expected := 1; target.stopCounter != expected {
		t.Fatalf("stopCounter expected %d, found %d", expected, target.stopCounter)
	}
}

func TestStartFails(t *testing.T) {
	target := &configurableStartAndStop{failToStart: true}
	_, err := Start(target)
	if err == nil {
		t.Fatalf("expected failure when starting: %T", target)
	}
	if !strings.Contains(err.Error(), "failed to start") {
		t.Fatalf("expected a different failure when starting: %s", err)
	}
	if expected := 0; target.startCounter != expected {
		t.Fatalf("startCounter expected %d, found %d", expected, target.startCounter)
	}
	if expected := 0; target.stopCounter != expected {
		t.Fatalf("stopCounter expected %d, found %d", expected, target.stopCounter)
	}
}

func TestStopFails(t *testing.T) {
	target := &configurableStartAndStop{failToStop: true}
	stopper, err := Start(target)
	if err != nil {
		t.Fatalf("when starting: %s", err)
	}
	err = stopper.Stop()
	if err == nil {
		t.Fatalf("expected failure when stopping: %T", target)
	}
	if !strings.Contains(err.Error(), "failed to stop") {
		t.Fatalf("expected a different failure when stopping: %s", err)
	}
	if expected := 1; target.startCounter != expected {
		t.Fatalf("startCounter expected %d, found %d", expected, target.startCounter)
	}
	if expected := 0; target.stopCounter != expected {
		t.Fatalf("stopCounter expected %d, found %d", expected, target.stopCounter)
	}
}

type someDependency interface{ specialMethodHere() }

type implOfSomeDependency struct{}

var _ someDependency = (*implOfSomeDependency)(nil)

func (*implOfSomeDependency) specialMethodHere() {}

type lateBoundDependency struct {
	SomeDependency someDependency `inject:""`
}

func TestLateBoundDependency(t *testing.T) {
	target := &lateBoundDependency{}
	stopper, err := Start(target, ImplementedBy{
		Target: reflect.TypeOf((*someDependency)(nil)).Elem(),
		Impl:   reflect.TypeOf((*implOfSomeDependency)(nil)),
	})
	if err != nil {
		t.Fatalf("when starting: %s", err)
	}
	if _, ok := target.SomeDependency.(*implOfSomeDependency); !ok {
		t.Fatalf("field SomeDependency has incorrect type: %T", target.SomeDependency)
	}
	if err := stopper.Stop(); err != nil {
		t.Fatalf("when stopping: %s", err)
	}
}

func TestLateBoundDependencyDoesNotImplement(t *testing.T) {
	target := &lateBoundDependency{}
	_, err := Start(target, ImplementedBy{
		Target: reflect.TypeOf((*someDependency)(nil)).Elem(),
		Impl:   reflect.TypeOf(implOfSomeDependency{}),
	})
	if err == nil {
		t.Fatalf("expected failure when starting: %T", target)
	}
	if !strings.Contains(err.Error(), "inject.implOfSomeDependency does not implement inject.someDependency") {
		t.Fatalf("expected a different failure when starting: %s", err)
	}
}

func TestLateBoundDependencyNotAnInterface(t *testing.T) {
	target := &lateBoundDependency{}
	_, err := Start(target, ImplementedBy{
		Target: reflect.TypeOf(map[string]struct{}{}),
		Impl:   reflect.TypeOf((*implOfSomeDependency)(nil)),
	})
	if err == nil {
		t.Fatalf("expected failure when starting: %T", target)
	}
	if !strings.Contains(err.Error(), "map[string]struct {} is not an interface") {
		t.Fatalf("expected a different failure when starting: %s", err)
	}
}

func checkStoppingOrder(t *testing.T, stopper Stopper, expectedOrder []reflect.Type) {
	t.Helper()
	var actualOrder []reflect.Type
	for _, node := range (stopper).(*graph).stoppingOrder {
		actualOrder = append(actualOrder, node.typ)
	}
	if !reflect.DeepEqual(actualOrder, expectedOrder) {
		t.Errorf(
			"stoppingOrder mismatch\nactual:   %s\nexpected: %s",
			typesToNiceString(actualOrder), typesToNiceString(expectedOrder))
	}
}

func typesToNiceString(types []reflect.Type) string {
	var parts []string
	for _, typ := range types {
		parts = append(parts, typ.String())
	}
	return strings.Join(parts, ", ")
}
