// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package notice

type Data struct {
	LibraryName string `json:"libraryName"`
	LicenseText []byte `json:"licenseText"`
	LineNumber  int    `json:"lineNumber"`
}
