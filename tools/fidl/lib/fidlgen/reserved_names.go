// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen

type NameContext struct {
	names map[string]struct{}
}

func NewNameContext() NameContext {
	return NameContext{make(map[string]struct{})}
}

func (nc NameContext) IsReserved(name string) bool {
	_, ok := nc.names[name]
	return ok
}

func (nc NameContext) ReserveNames(names []string) {
	for _, n := range names {
		nc.names[n] = struct{}{}
	}
}
