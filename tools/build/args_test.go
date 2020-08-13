// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

import (
	"encoding/json"
	"testing"
)

func TestArgs(t *testing.T) {

	argsJSON := []byte(`
	{
		"bool_var": true
	}`)
	var args Args
	if err := json.Unmarshal(argsJSON, &args); err != nil {
		t.Fatalf("failed to unmarshal arguments: %v", err)
	}

	val, err := args.BoolValue("bool_var")
	if err != nil {
		t.Fatalf("failed to determine value of boolean argument: %v", err)
	} else if val != true {
		t.Fatalf("expected the value under |bool_var| to be true")
	}

	val, err = args.BoolValue("nonexistent_var")
	if err != ErrArgNotSet {
		t.Fatalf("expected ErrArgNotSet and not %v", err)
	}
}
