// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package zither_test

import (
	"fmt"
	"testing"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgentest"
	"go.fuchsia.dev/fuchsia/zircon/tools/zither"
)

func TestCanSummarizeLibraryName(t *testing.T) {
	name := "this.is.an.example.library"
	ir := fidlgentest.EndToEndTest{T: t}.Single(fmt.Sprintf("library %s;", name))
	sum, err := zither.NewSummary(ir)
	if err != nil {
		t.Fatal(err)
	}
	if sum.Name.String() != name {
		t.Errorf("expected %s; got %s", name, sum.Name)
	}
}
