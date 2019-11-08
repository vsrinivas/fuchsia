// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package transformer

import (
	"fmt"
	"io"
	"strings"
	"text/template"

	fidlcommon "fidl/compiler/backend/common"
	fidlir "fidl/compiler/backend/types"
	gidlir "gidl/ir"
)

var tmpl = template.Must(template.New("tmpls").Parse(`
#include <unittest/unittest.h>

#include "generated/transformer_conformance_tables.h"
#include "transformer_conformance_utils.h"

namespace {

{{ range .TestCases }}
uint8_t {{ .Name }}_old_bytes[] = { {{ .OldBytes }} };

uint8_t {{ .Name }}_v1_bytes[] = { {{ .V1Bytes }} };
{{ end }}

{{ range .TestCases }}
bool test_{{ .Name }}_old_to_v1() {
	BEGIN_TEST;
	ASSERT_TRUE(check_fidl_transform(
		FIDL_TRANSFORMATION_OLD_TO_V1,
		&{{ .OldFidlType }},
		{{ .Name }}_old_bytes, sizeof {{ .Name }}_old_bytes,
		{{ .Name }}_v1_bytes, sizeof {{ .Name }}_v1_bytes
	));
	END_TEST;
}

bool test_{{ .Name }}_v1_to_old() {
	BEGIN_TEST;
	ASSERT_TRUE(check_fidl_transform(
		FIDL_TRANSFORMATION_V1_TO_OLD,
		&{{ .V1FidlType }},
		{{ .Name }}_v1_bytes, sizeof {{ .Name }}_v1_bytes,
		{{ .Name }}_old_bytes, sizeof {{ .Name }}_old_bytes
	));
	END_TEST;
}
{{ end }}

} // namespace

BEGIN_TEST_CASE(transformer_conformance)
{{ range .TestCases }}
RUN_TEST(test_{{ .Name }}_old_to_v1)
RUN_TEST(test_{{ .Name }}_v1_to_old)
{{ end }}
END_TEST_CASE(transformer_conformance)
`))

type tmplInput struct {
	TestCases []testCase
}

type testCase struct {
	Name, OldFidlType, V1FidlType, OldBytes, V1Bytes string
}

// Generate generates transformer tests.
func Generate(wr io.Writer, gidl gidlir.All, fidl fidlir.Root) error {
	input := tmplInput{
		TestCases: testCases(gidl.EncodeSuccess),
	}
	return tmpl.Execute(wr, input)
}

func testCases(gidlEncodeSuccesses []gidlir.EncodeSuccess) []testCase {
	v1BytesByName := make(map[string][]byte)
	for _, encodeSuccess := range gidlEncodeSuccesses {
		if encodeSuccess.WireFormat != gidlir.V1WireFormat {
			continue
		}
		v1BytesByName[encodeSuccess.Name] = encodeSuccess.Bytes
	}

	var testCases []testCase
	for _, encodeSuccess := range gidlEncodeSuccesses {
		if encodeSuccess.WireFormat != gidlir.OldWireFormat {
			continue
		}
		v1Name := gidlir.TestCaseName(encodeSuccess.Name, gidlir.V1WireFormat)
		v1Bytes, ok := v1BytesByName[v1Name]
		if !ok {
			continue
		}
		typeName := encodeSuccess.Value.(gidlir.Object).Name
		testCases = append(testCases, testCase{
			Name:        fidlcommon.ToSnakeCase(encodeSuccess.Name),
			OldFidlType: fidlTypeName(gidlir.OldWireFormat, typeName),
			V1FidlType:  fidlTypeName(gidlir.V1WireFormat, typeName),
			OldBytes:    bytesBuilder(encodeSuccess.Bytes),
			V1Bytes:     bytesBuilder(v1Bytes),
		})
	}
	return testCases
}

func fidlTypeName(wireFormat gidlir.WireFormat, typeName string) string {
	switch wireFormat {
	case gidlir.OldWireFormat:
		return fmt.Sprintf("conformance_%sTable", typeName)
	case gidlir.V1WireFormat:
		return fmt.Sprintf("v1_conformance_%sTable", typeName)
	default:
		panic(fmt.Sprintf("unexpected wire format %v", wireFormat))
	}
}

// TODO(fxb/39685) extract out to common library
func bytesBuilder(bytes []byte) string {
	var builder strings.Builder
	for i, b := range bytes {
		builder.WriteString(fmt.Sprintf("0x%02x", b))
		builder.WriteString(",")
		if i%8 == 7 {
			builder.WriteString("//\n")
		}
	}
	return builder.String()
}
