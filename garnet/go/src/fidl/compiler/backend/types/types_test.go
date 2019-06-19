// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package types

import (
	"encoding/json"
	"math"
	"testing"
)

func TestCanUnmarshalLargeOrdinal(t *testing.T) {
	input := `{
		"ordinal": 18446744073709551615,
		"generated_ordinal": 18446744073709551615
	}`

	var method Method
	err := json.Unmarshal([]byte(input), &method)
	if err != nil {
		t.Fatalf("failed to unmarshal: %s", err)
	}
	if method.Ordinal != math.MaxUint64 {
		t.Fatalf("method.Ordinal: expected math.MaxUint64, found %d", method.Ordinal)
	}
	if method.GenOrdinal != math.MaxUint64 {
		t.Fatalf("method.GenOrdinal: expected math.MaxUint64, found %d", method.GenOrdinal)
	}
}
