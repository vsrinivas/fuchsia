// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package types

import "fmt"

// TODO(FIDL-524): Remove post migration.

type methodOrdinal uint64

func (wrapper methodOrdinal) String() string {
	return fmt.Sprintf("%#x", uint64(wrapper))
}

// NamedOrdinal represents a named ordinal.
type NamedOrdinal struct {
	Name    string
	Ordinal methodOrdinal
}

// Ordinals represents the two ordinals ord and gen as they move through
// their migration. It exposes convenience methods to have generic code
// generation, and control the status of the migration in one central place.
//
// Read more about migration strategy on FIDL-524.
type Ordinals struct {
	ord NamedOrdinal
	gen NamedOrdinal
}

// NewOrdinals creates an Ordinals using the `ordName`, and `genName` provided.
func NewOrdinals(method Method, ordName, genName string) Ordinals {
	return Ordinals{
		ord: NamedOrdinal{
			Name:    ordName,
			Ordinal: methodOrdinal(method.Ordinal << 32),
		},
		gen: NamedOrdinal{
			Name:    genName,
			Ordinal: methodOrdinal(method.GenOrdinal << 32),
		},
	}
}

// Reads returns all distinct ordinals to be used on read, i.e. either
// ord, gen, or both, depending on the current status of the migration.
func (ords Ordinals) Reads() []NamedOrdinal {
	var reads []NamedOrdinal
	if ords.ord.Ordinal != 0 {
		reads = append(reads, ords.ord)
	}
	if ords.gen.Ordinal != 0 {
		if ords.gen.Ordinal != ords.ord.Ordinal {
			reads = append(reads, ords.gen)
		}
	}
	if len(reads) == 0 {
		panic(fmt.Sprintf("illegal migration step, no read ordinal: %v", ords))
	}
	return reads
}

// Write returns the ordinal to be used on write, i.e. either ord or gen,
// depending on the current status of the migration.
func (ords Ordinals) Write() NamedOrdinal {
	return ords.Reads()[0]
}
