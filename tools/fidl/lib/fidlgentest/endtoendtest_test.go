// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgentest

import (
	"testing"
)

func TestEndToEndExample(t *testing.T) {
	root := EndToEndTest{T: t}.Single(`library example;

	struct MyStruct {
		string field1;
		string field2;
	};`)

	if root.Name != "example" {
		t.Errorf("expected 'example', was '%s'", root.Name)
	}
}
