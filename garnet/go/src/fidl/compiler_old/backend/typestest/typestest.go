// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(fxb/55387): Temporary, will be removed.

package typestest

import (
	"testing"

	"go.fuchsia.dev/fuchsia/garnet/go/src/fidl/compiler/backend/types"
	"go.fuchsia.dev/fuchsia/garnet/go/src/fidl/compiler/backend/typestest"
)

func AllExamples(basePath string) []string {
	return typestest.AllExamples(basePath)
}

func GetExample(basePath, filename string) types.Root {
	return typestest.GetExample(basePath, filename)
}

func GetGolden(basePath, filename string) []byte {
	return typestest.GetGolden(basePath, filename)
}

func AssertCodegenCmp(t *testing.T, want, got []byte) {
	typestest.AssertCodegenCmp(t, want, got)
}
