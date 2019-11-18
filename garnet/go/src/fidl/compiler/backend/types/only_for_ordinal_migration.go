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
	ord      NamedOrdinal
	gen      NamedOrdinal
	writeGen bool
}

// NewOrdinalsStep3 creates an Ordinals using the `ordName`, and `genName`
// provided.
//
// Step #3 of migration: ord << 32, gen is not shifted (read from fidlc
// directly)
func NewOrdinalsStep3(method Method, ordName, genName string) Ordinals {
	return Ordinals{
		ord: NamedOrdinal{
			Name:    ordName,
			Ordinal: methodOrdinal(method.Ordinal << 32),
		},
		gen: NamedOrdinal{
			Name:    genName,
			Ordinal: methodOrdinal(method.GenOrdinal),
		},
		writeGen: true,
	}
}

// NewOrdinalsStep5 creates an Ordinals using the `ordName`, and `genName`
// provided.
//
// Step #5 of migration: neither ord nor gen are shifted (they are read directly
// from fidlc)
func NewOrdinalsStep5(method Method, ordName, genName string) Ordinals {
	return Ordinals{
		ord: NamedOrdinal{
			Name:    ordName,
			Ordinal: methodOrdinal(method.Ordinal),
		},
		gen: NamedOrdinal{
			Name:    genName,
			Ordinal: methodOrdinal(method.GenOrdinal),
		},
	}
}

// NewOrdinalsStep7 creates an Ordinals using the `ordName`, and `genName`
// provided.
//
// Step #7 of migration: neither ord nor gen are shifted (they are read directly
// from fidlc). Send gen, which is 64b version.
func NewOrdinalsStep7(method Method, ordName, genName string) Ordinals {
	return Ordinals{
		ord: NamedOrdinal{
			Name:    ordName,
			Ordinal: methodOrdinal(method.Ordinal),
		},
		gen: NamedOrdinal{
			Name:    genName,
			Ordinal: methodOrdinal(method.GenOrdinal),
		},
		writeGen: true,
	}
}

// Reads returns all distinct ordinals to be used on read, i.e. either
// ord, gen, or both, depending on the current status of the migration.
func (ords Ordinals) Reads() []NamedOrdinal {
	var (
		reads      []NamedOrdinal
		includeOrd = ords.ord.Ordinal != 0
		includeGen = ords.gen.Ordinal != 0
	)
	if includeOrd && includeGen && ords.ord.Ordinal == ords.gen.Ordinal {
		if ords.writeGen {
			reads = append(reads, ords.gen)
		} else {
			reads = append(reads, ords.ord)
		}
	} else {
		if includeOrd {
			reads = append(reads, ords.ord)
		}
		if includeGen {
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
	if ords.writeGen {
		return ords.gen
	} else {
		return ords.ord
	}
}
