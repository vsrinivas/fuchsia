// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package summarize

import (
	"testing"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

func intptr(i int) *int {
	return &i
}

func TestSymboltable(t *testing.T) {
	tests := []struct {
		name     string
		type_    fidlgen.Type
		expected Decl
	}{
		{
			name: "optional string",
			type_: fidlgen.Type{
				Kind:     fidlgen.StringType,
				Nullable: true,
			},
			expected: "string:optional",
		},
		{
			name: "optional string with max size",
			type_: fidlgen.Type{
				Kind:         fidlgen.StringType,
				ElementCount: intptr(10),
				Nullable:     true,
			},
			expected: "string:<10,optional>",
		},
		{
			name: "plain identifier",
			type_: fidlgen.Type{
				Kind:       fidlgen.IdentifierType,
				Identifier: "foo/struct",
			},
			expected: "foo/struct",
		},
		{
			name: "optional identifier, not naturally optional",
			type_: fidlgen.Type{
				Kind:       fidlgen.IdentifierType,
				Identifier: "foo/struct",
				Nullable:   true,
			},
			expected: "box<foo/struct>",
		},
		{
			name: "optional identifier",
			type_: fidlgen.Type{
				Kind:       fidlgen.IdentifierType,
				Identifier: "foo/bar",
				Nullable:   true,
			},
			expected: "foo/bar:optional",
		},
		{
			name: "array",
			type_: fidlgen.Type{
				Kind:         fidlgen.ArrayType,
				ElementCount: intptr(10),
				ElementType: &fidlgen.Type{
					Kind: fidlgen.StringType,
				},
			},
			expected: "array<string,10>",
		},
		{
			name: "vector",
			type_: fidlgen.Type{
				Kind:         fidlgen.VectorType,
				ElementCount: intptr(10),
				ElementType: &fidlgen.Type{
					Kind: fidlgen.StringType,
				},
			},
			expected: "vector<string>:10",
		},
		{
			name: "optional vector",
			type_: fidlgen.Type{
				Kind:         fidlgen.VectorType,
				ElementCount: intptr(10),
				ElementType: &fidlgen.Type{
					Kind: fidlgen.StringType,
				},
				Nullable: true,
			},
			expected: "vector<string>:<10,optional>",
		},
		{
			name: "protocol, client end",
			type_: fidlgen.Type{
				Kind:       fidlgen.IdentifierType,
				Identifier: "foo/protocol",
			},
			expected: "client_end:foo/protocol",
		},
		{
			name: "protocol, client end optional",
			type_: fidlgen.Type{
				Kind:       fidlgen.IdentifierType,
				Identifier: "foo/protocol",
				Nullable:   true,
			},
			expected: "client_end:<foo/protocol,optional>",
		},
		{
			name: "protocol, server end",
			type_: fidlgen.Type{
				Kind:           fidlgen.RequestType,
				RequestSubtype: "foo/protocol",
			},
			expected: "server_end:foo/protocol",
		},
		{
			name: "protocol, server end, optional",
			type_: fidlgen.Type{
				Kind:           fidlgen.RequestType,
				RequestSubtype: "foo/protocol",
				Nullable:       true,
			},
			expected: "server_end:<foo/protocol,optional>",
		},
		{
			name: "handle with rights, optional",
			type_: fidlgen.Type{
				Kind:          fidlgen.HandleType,
				HandleRights:  fidlgen.HandleRightsBasic | fidlgen.HandleRightsApplyProfile,
				HandleSubtype: fidlgen.Interrupt,
				Nullable:      true,
			},
			expected: "zx/handle:<INTERRUPT,zx.DUPLICATE,zx.TRANSFER,zx.WAIT,zx.INSPECT,zx.APPLY_PROFILE,optional>",
		},
	}
	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			var s symbolTable
			// Use these special names to test struct and protocol behaviors.
			s.addStruct("foo/struct", &fidlgen.Struct{})
			s.addProtocol("foo/protocol")

			actual := s.fidlTypeString(test.type_)
			if actual != test.expected {
				t.Errorf("want: %+v, got: %+v\n\tfor test: %+v", test.expected, actual, test)
			}
		})
	}
}
