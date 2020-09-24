// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fake_clock

// TODO(fxbug.dev/45644): Disabled due to flake
// // #cgo LDFLAGS: -lfake_clock
// import "C"

// This file is required to allow the go code to link over to the non-go
// fake_clock library. #cgo can't be put in a test file which is why it's
// here!
