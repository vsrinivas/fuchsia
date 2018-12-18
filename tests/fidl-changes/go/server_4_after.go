// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fidl/fidl/test/after"
)

type after_addMethodImpl struct{}

// Assert that after_addMethodImpl implements the AddMethod interface
var _ after.AddMethod = &after_addMethodImpl{}

func (_ *after_addMethodImpl) ExistingMethod() error {
	return nil
}
func (_ *after_addMethodImpl) NewMethod() error {
	return nil
}

type after_removeMethodImpl struct {
	after.RemoveMethodTransitionalBase
}

// Assert that after_removeMethodImpl implements the RemoveMethod interface
var _ after.RemoveMethod = &after_removeMethodImpl{}

func (_ *after_removeMethodImpl) ExistingMethod() error {
	return nil
}

type after_addEventImpl struct{}

// Assert that after_addEventImpl implements the AddEvent interface
var _ after.AddEvent = &after_addEventImpl{}

func (_ *after_addEventImpl) ExistingMethod() error {
	return nil
}

type after_removeEventImpl struct{}

// Assert that after_removeEventImpl implements the RemoveEvent interface
var _ after.RemoveEvent = &after_removeEventImpl{}

func (_ *after_removeEventImpl) ExistingMethod() error {
	return nil
}
