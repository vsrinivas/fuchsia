// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fidl/fidl/test/during"
	"syscall/zx/fidl"
)

type before_during_addMethodImpl struct {
	during.AddMethodWithCtxTransitionalBase
}

// Assert that before_during_addMethodImpl implements the AddMethod interface
var _ during.AddMethodWithCtx = &before_during_addMethodImpl{}

func (_ *before_during_addMethodImpl) ExistingMethod(fidl.Context) error {
	return nil
}

type before_during_removeMethodImpl struct{}

// Assert that before_during_removeMethodImpl implements the RemoveMethod interface
var _ during.RemoveMethodWithCtx = &before_during_removeMethodImpl{}

func (_ *before_during_removeMethodImpl) ExistingMethod(fidl.Context) error {
	return nil
}

func (_ *before_during_removeMethodImpl) OldMethod(fidl.Context) error {
	return nil
}

type before_during_addEventImpl struct{}

// Assert that before_during_addEventImpl implements the AddEvent interface
var _ during.AddEventWithCtx = &before_during_addEventImpl{}

func (_ *before_during_addEventImpl) ExistingMethod(fidl.Context) error {
	return nil
}

type before_during_removeEventImpl struct{}

// Assert that before_during_removeEventImpl implements the RemoveEvent interface
var _ during.RemoveEventWithCtx = &before_during_removeEventImpl{}

func (_ *before_during_removeEventImpl) ExistingMethod(fidl.Context) error {
	return nil
}
