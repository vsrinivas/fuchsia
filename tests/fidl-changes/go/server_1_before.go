// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fidl/fidl/test/before"
)

type before_addMethodImpl struct {
	before.AddMethodTransitionalBase
}

// Assert that before_addMethodImpl implements the AddMethod interface
var _ before.AddMethod = &before_addMethodImpl{}

func (_ *before_addMethodImpl) ExistingMethod() error {
	return nil
}

type before_removeMethodImpl struct{}

// Assert that before_removeMethodImpl implements the RemoveMethod interface
var _ before.RemoveMethod = &before_removeMethodImpl{}

func (_ *before_removeMethodImpl) ExistingMethod() error {
	return nil
}

func (_ *before_removeMethodImpl) OldMethod() error {
	return nil
}

type before_addEventImpl struct{}

// Assert that before_addEventImpl implements the AddEvent interface
var _ before.AddEvent = &before_addEventImpl{}

func (_ *before_addEventImpl) ExistingMethod() error {
	return nil
}

type before_removeEventImpl struct{}

// Assert that before_removeEventImpl implements the RemoveEvent interface
var _ before.RemoveEvent = &before_removeEventImpl{}

func (_ *before_removeEventImpl) ExistingMethod() error {
	return nil
}
