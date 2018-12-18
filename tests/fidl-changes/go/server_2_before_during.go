// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fidl/fidl/test/during"
)

type before_during_addMethodImpl struct {
	during.AddMethodTransitionalBase
}

// Assert that before_during_addMethodImpl implements the AddMethod interface
var _ during.AddMethod = &before_during_addMethodImpl{}

func (_ *before_during_addMethodImpl) ExistingMethod() error {
	return nil
}

type before_during_removeMethodImpl struct{}

// Assert that before_during_removeMethodImpl implements the RemoveMethod interface
var _ during.RemoveMethod = &before_during_removeMethodImpl{}

func (_ *before_during_removeMethodImpl) ExistingMethod() error {
	return nil
}

func (_ *before_during_removeMethodImpl) OldMethod() error {
	return nil
}

type before_during_addEventImpl struct{}

// Assert that before_during_addEventImpl implements the AddEvent interface
var _ during.AddEvent = &before_during_addEventImpl{}

func (_ *before_during_addEventImpl) ExistingMethod() error {
	return nil
}

type before_during_removeEventImpl struct{}

// Assert that before_during_removeEventImpl implements the RemoveEvent interface
var _ during.RemoveEvent = &before_during_removeEventImpl{}

func (_ *before_during_removeEventImpl) ExistingMethod() error {
	return nil
}
