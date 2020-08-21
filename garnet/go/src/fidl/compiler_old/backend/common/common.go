// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(fxb/55387): Temporary, will be removed.

package common

import (
	"go.fuchsia.dev/fuchsia/garnet/go/src/fidl/compiler/backend/common"
)

func NewFormatter(path string, args ...string) common.Formatter {
	return common.NewFormatter(path, args...)
}
func ToUpperCamelCase(name string) string {
	return common.ToUpperCamelCase(name)
}
func ToLowerCamelCase(name string) string {
	return common.ToLowerCamelCase(name)
}
