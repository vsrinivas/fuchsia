// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package rustgen

import (
	"strings"

	"fidl/compiler/generated/fidl_files"
)


// Converts a namespace such as "my_module.your_module" into ["my_module", "your_module"]
func GetNamespace(mojomFile *fidl_files.FidlFile) []string {
	if mojomFile.ModuleNamespace != nil {
		return strings.Split(*mojomFile.ModuleNamespace, ".")
	}
	return make([]string, 0)
}
