// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package clock allows the current time to be overloaded
package clock

import (
	"time"
)

// An overridable method to get the current time.
var Now = time.Now
