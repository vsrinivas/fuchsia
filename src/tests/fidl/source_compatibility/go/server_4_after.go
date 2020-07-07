// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fidl/fidl/test/after"
	"syscall/zx/fidl"
)

type after_addMethodImpl struct{}

// Assert that after_addMethodImpl implements the AddMethod interface
var _ after.AddMethodWithCtx = &after_addMethodImpl{}

func (_ *after_addMethodImpl) ExistingMethod(fidl.Context) error {
	return nil
}
func (_ *after_addMethodImpl) NewMethod(fidl.Context) error {
	return nil
}

type after_removeMethodImpl struct {
	after.RemoveMethodWithCtxTransitionalBase
}

// Assert that after_removeMethodImpl implements the RemoveMethod interface
var _ after.RemoveMethodWithCtx = &after_removeMethodImpl{}

func (_ *after_removeMethodImpl) ExistingMethod(fidl.Context) error {
	return nil
}

type after_addEventImpl struct{}

// Assert that after_addEventImpl implements the AddEvent interface
var _ after.AddEventWithCtx = &after_addEventImpl{}

func (_ *after_addEventImpl) ExistingMethod(fidl.Context) error {
	return nil
}

type after_removeEventImpl struct{}

// Assert that after_removeEventImpl implements the RemoveEvent interface
var _ after.RemoveEventWithCtx = &after_removeEventImpl{}

func (_ *after_removeEventImpl) ExistingMethod(fidl.Context) error {
	return nil
}
