// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

import (
	"encoding/json"
	"errors"
	"fmt"
	"os"
)

var (
	// ErrArgNotSet represents an arg not having been set in the build.
	ErrArgNotSet = errors.New("arg not set")
)

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

// TODO(you): extend to different types as needed.

func loadArgs(manifest string) (Args, error) {
	f, err := os.Open(manifest)
	if err != nil {
		return nil, fmt.Errorf("failed to open %s: %w", manifest, err)
	}
	defer f.Close()
	var args Args
	if err := json.NewDecoder(f).Decode(&args); err != nil {
		return nil, fmt.Errorf("failed to decode %s: %w", manifest, err)
	}
	return args, nil
}
