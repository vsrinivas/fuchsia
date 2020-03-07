// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package lib

import (
	"testing"

	"github.com/google/go-cmp/cmp"
)

type handleTestObj struct {
	StringParam string
	IntParam    int

	privateParam string
}

type handleTestObj2 struct {
	OtherParam int
}

func TestHandleSerialize(t *testing.T) {
	obj := &handleTestObj{StringParam: "フクシャ", IntParam: 2147483647, privateParam: "shhh"}

	handle, err := NewHandleFromObjects(obj)
	if err != nil {
		t.Fatalf("error creating handle: %s", err)
	}

	s := handle.Serialize()
	reloadedHandle, err := LoadHandleFromString(s)
	if err != nil {
		t.Fatalf("error deserializing handle: %s", err)
	}

	var result handleTestObj
	if err := reloadedHandle.PopulateObject(&result); err != nil {
		t.Fatalf("error populating object: %s", err)
	}

	want := &handleTestObj{StringParam: obj.StringParam, IntParam: obj.IntParam}
	if diff := cmp.Diff(want, &result, cmp.AllowUnexported(handleTestObj{})); diff != "" {
		t.Fatalf("incorrect reloaded handle (-want +got):\n%s", diff)
	}
}

func TestHandleMerge(t *testing.T) {
	obj := &handleTestObj{StringParam: "フクシャ", IntParam: 2147483647, privateParam: "shhh"}
	obj2 := &handleTestObj2{OtherParam: 0x45}

	handle, err := NewHandleFromObjects(obj, nil, obj2)
	if err != nil {
		t.Fatalf("error creating handle: %s", err)
	}

	var result handleTestObj
	var result2 handleTestObj2
	if err := handle.PopulateObject(&result); err != nil {
		t.Fatalf("error populating obj: %s", err)
	}
	if err := handle.PopulateObject(&result2); err != nil {
		t.Fatalf("error populating obj2: %s", err)
	}

	want := &handleTestObj{StringParam: obj.StringParam, IntParam: obj.IntParam}
	if diff := cmp.Diff(want, &result, cmp.AllowUnexported(handleTestObj{})); diff != "" {
		t.Fatalf("incorrect result in obj (-want +got):\n%s", diff)
	}

	if diff := cmp.Diff(obj2, &result2); diff != "" {
		t.Fatalf("incorrect result in obj2 (-want +got):\n%s", diff)
	}
}
