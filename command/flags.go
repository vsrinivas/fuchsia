// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package command

import (
	"strings"
)

// StringsFlag implements flag.Value so it may be treated as a flag type.
type StringsFlag []string

// Set implements flag.Value.Set.
func (s *StringsFlag) Set(val string) error {
	*s = append(*s, val)
	return nil
}

// Strings implements flag.Value.String.
func (s *StringsFlag) String() string {
	if s == nil {
		return ""
	}
	return strings.Join([]string(*s), ", ")
}
