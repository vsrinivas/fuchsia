// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen

type NameChanger func(string) string
type NameContext struct {
	reserved map[string]struct{}
	changer  NameChanger
}

func NewNameContext(changer NameChanger) NameContext {
	return NameContext{
		reserved: make(map[string]struct{}),
		changer:  changer,
	}
}

func ReserveNames(names map[string][]NameContext) {
	for name, contexts := range names {
		for _, context := range contexts {
			context.reserved[name] = struct{}{}
		}
	}
}

func (c *NameContext) IsReserved(name string) bool {
	_, ok := c.reserved[name]
	return ok
}

func (c *NameContext) ChangeIfReserved(name string) string {
	if c.IsReserved(name) {
		return c.changer(name)
	}
	return name
}
