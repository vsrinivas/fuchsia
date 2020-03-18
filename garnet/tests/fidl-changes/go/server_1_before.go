// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fidl/fidl/test/before"
	"syscall/zx/fidl"
)

type before_addMethodImpl struct {
	before.AddMethodWithCtxTransitionalBase
}

// Assert that before_addMethodImpl implements the AddMethod interface
var _ before.AddMethodWithCtx = &before_addMethodImpl{}

func (_ *before_addMethodImpl) ExistingMethod(fidl.Context) error {
	return nil
}

type before_removeMethodImpl struct{}

// Assert that before_removeMethodImpl implements the RemoveMethod interface
var _ before.RemoveMethodWithCtx = &before_removeMethodImpl{}

func (_ *before_removeMethodImpl) ExistingMethod(fidl.Context) error {
	return nil
}

func (_ *before_removeMethodImpl) OldMethod(fidl.Context) error {
	return nil
}

type before_addEventImpl struct{}

// Assert that before_addEventImpl implements the AddEvent interface
var _ before.AddEventWithCtx = &before_addEventImpl{}

func (_ *before_addEventImpl) ExistingMethod(fidl.Context) error {
	return nil
}

type before_removeEventImpl struct{}

// Assert that before_removeEventImpl implements the RemoveEvent interface
var _ before.RemoveEventWithCtx = &before_removeEventImpl{}

func (_ *before_removeEventImpl) ExistingMethod(fidl.Context) error {
	return nil
}
