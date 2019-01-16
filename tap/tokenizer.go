// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package tap

// Directive is a TAP directive (TODO|SKIP|<none>)
type Directive int

// Valid Tap directives.
const (
	None Directive = iota
	Todo
	Skip
)
