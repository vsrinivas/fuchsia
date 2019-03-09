// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package artifacts

import (
	"strings"
)

// Constants used to replace text in GCS object path segments.
const (
	// iFwdSlashSub is used in place of inner forward slashes.
	iFwdSlashSub = "."

	// iSpaceSub is used in place of inner \s characters.
	iSpaceSub = "_"
)

// NormalizePathSegment rewrites a string so that it's safe to use as a GCS Object path.
// This is useful for creating object paths from things like test names, which often
// correspond to build target names and containing leading and inner forward slashes; Or
// environment names, which often contain spaces or slashes for separating version
// numbers.
func normalizePathSegment(segment string) string {
	segment = strings.TrimPrefix(segment, "//")
	segment = strings.TrimSpace(segment)
	segment = strings.Replace(segment, "/", iFwdSlashSub, -1)
	segment = strings.Replace(segment, " ", iSpaceSub, -1)
	return segment
}
