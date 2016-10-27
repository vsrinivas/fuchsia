// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

// These declarations go in the header file so that we can avoid some
// circular-dependencies. We call these "public" to say that other types can
// refer to them.
const GenerateTypeTableDeclarations = `
{{define "GenerateTypeTableDeclarations"}}
// Union type table declarations.
{{range $union := .PublicUnionNames -}}
extern struct MojomTypeDescriptorUnion {{$union}};
{{end -}}

// Struct type table declarations.
{{range $struct := .PublicStructNames -}}
extern struct MojomTypeDescriptorStruct {{$struct}};
{{end -}}
{{end}}
`

const GenerateTypeTableDefinitions = `
{{define "GenerateTypeTableDefinitions"}}
// Declarations for array type entries.
{{range $array := .Arrays -}}
static struct MojomTypeDescriptorArray {{$array.Name}};
{{end -}}

// Declarations for struct type tables.
{{range $struct := .Structs -}}
struct MojomTypeDescriptorStruct {{$struct.Name}};
{{end -}}

// Declarations for union type tables.
{{range $union := .Unions -}}
struct MojomTypeDescriptorUnion {{$union.Name}};
{{end -}}

// Array type entry definitions.
{{range $array := .Arrays -}}
static struct MojomTypeDescriptorArray {{$array.Name}} = {
  .elem_type = {{$array.ElemType}},
  .elem_descriptor = {{$array.ElemTable}},
  .elem_num_bits = {{$array.ElemNumBits}},
  .num_elements = {{$array.NumElements}},
  .nullable = {{$array.Nullable}},
};
{{end -}}

// Struct type table definitions.
{{range $struct := .Structs -}}
struct MojomTypeDescriptorStructEntry {{$struct.Name}}_Entries[] = {
{{- range $entry := $struct.Entries}}
  {
    .elem_type = {{$entry.ElemType}},
    .elem_descriptor = {{$entry.ElemTable}},
    .offset = {{$entry.Offset}},
    .min_version = {{$entry.MinVersion}},
    .nullable = {{$entry.Nullable}},
  },
{{end -}}
};

struct MojomTypeDescriptorStructVersion {{$struct.Name}}_Versions[] = {
{{- range $version := $struct.Versions}}
  { .version = {{$version.Version}}, .num_bytes = {{$version.NumBytes}} },
{{end -}}
};

struct MojomTypeDescriptorStruct {{$struct.Name}} = {
  .num_versions = {{len $struct.Versions}}ul,
  .versions = {{$struct.Name}}_Versions,
  .num_entries = {{len $struct.Entries}}ul,
  .entries = {{$struct.Name}}_Entries,
};
{{end -}}

// Union type table definitions.
{{range $union := .Unions -}}
struct MojomTypeDescriptorUnionEntry {{$union.Name}}_Entries[] = {
{{- range $entry := $union.Entries}}
  {
    .elem_type = {{$entry.ElemType}},
    .elem_descriptor = {{$entry.ElemTable}},
    .tag = {{$entry.Tag}},
    .nullable = {{$entry.Nullable}},
  },
{{end -}}
};
struct MojomTypeDescriptorUnion {{$union.Name}} = {
  .num_fields = {{$union.NumFields}}ul,
  .num_entries = {{len $union.Entries}}ul,
  .entries = {{$union.Name}}_Entries,
};
{{end}}

{{end}}
`
