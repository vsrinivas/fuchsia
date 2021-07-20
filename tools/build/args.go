// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

import (
	"encoding/json"
	"errors"
)

// ErrArgNotSet represents an arg not having been set in the build.
var ErrArgNotSet = errors.New("arg not set")

// Args represents the GN arguments set in the build.
type Args map[string]json.RawMessage

// BoolValue returns the value of a boolean GN arg set in the build. If unset,
// ErrArgNotSet will be returned.
func (args Args) BoolValue(name string) (bool, error) {
	msg, ok := args[name]
	if !ok {
		return false, ErrArgNotSet
	}
	var val bool
	err := json.Unmarshal(msg, &val)
	return val, err
}
