// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package platform

import "fmt"

type Platform int

const (
	_ Platform = iota
	NS2
)

func (p Platform) String() string {
	switch p {
	case NS2:
		return "NS2"
	default:
		panic(fmt.Sprintf("Unrecognized Platform: %d", p))
	}
}
