// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package utils

import (
	"sort"

	"go.fuchsia.dev/fuchsia/tools/fidl/measure-tape/src/measurer"
)

// ForAllMethodsInOrder iterates over allMethods in the order of MethodID.
func ForAllMethodsInOrder(
	allMethods map[measurer.MethodID]*measurer.Method,
	fn func(m *measurer.Method)) {

	methodsToPrint := make([]measurer.MethodID, 0, len(allMethods))
	for id := range allMethods {
		methodsToPrint = append(methodsToPrint, id)
	}
	sort.Sort(measurer.ByTargetTypeThenKind(methodsToPrint))
	for _, id := range methodsToPrint {
		fn(allMethods[id])
	}
}

// ForAllVariantsInOrder iterates over variants in member order. If present,
// the unknown variant is always last.
func ForAllVariantsInOrder(
	variants map[string]measurer.LocalWithBlock,
	fn func(member string, localWithBlock measurer.LocalWithBlock)) {

	var members []string
	for member := range variants {
		members = append(members, member)
	}
	sort.Strings(members)
	for _, member := range members {
		if member != measurer.UnknownVariant {
			fn(member, variants[member])
		}
	}
	if _, ok := variants[measurer.UnknownVariant]; ok {
		fn(measurer.UnknownVariant, variants[measurer.UnknownVariant])
	}
}
