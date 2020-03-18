// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fidl/fidl/test/during"
	"syscall/zx/fidl"
)

type after_during_addMethodImpl struct{}

// Assert that after_during_addMethodImpl implements the AddMethod interface
var _ during.AddMethodWithCtx = &after_during_addMethodImpl{}

func (_ *after_during_addMethodImpl) ExistingMethod(fidl.Context) error {
	return nil
}
func (_ *after_during_addMethodImpl) NewMethod(fidl.Context) error {
	return nil
}

type after_during_removeMethodImpl struct {
	during.RemoveMethodWithCtxTransitionalBase
}

// Assert that after_during_removeMethodImpl implements the RemoveMethod interface
var _ during.RemoveMethodWithCtx = &after_during_removeMethodImpl{}

func (_ *after_during_removeMethodImpl) ExistingMethod(fidl.Context) error {
	return nil
}

type after_during_addEventImpl struct{}

// Assert that after_during_addEventImpl implements the AddEvent interface
var _ during.AddEventWithCtx = &after_during_addEventImpl{}

func (_ *after_during_addEventImpl) ExistingMethod(fidl.Context) error {
	return nil
}

type after_during_removeEventImpl struct{}

// Assert that after_during_removeEventImpl implements the RemoveEvent interface
var _ during.RemoveEventWithCtx = &after_during_removeEventImpl{}

func (_ *after_during_removeEventImpl) ExistingMethod(fidl.Context) error {
	return nil
}
