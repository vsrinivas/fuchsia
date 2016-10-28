// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const GenerateUnion = `
{{/* . (dot) refers to a the Go type |cgen.UnionTemplate|  */}}
{{define "GenerateUnion"}}
{{- $union := . -}}

// -- {{$union.Name}} --
// Enum describing the union tags.
{{template "GenerateEnum" $union.TagsEnum}}

struct {{$union.Name}} {
  uint32_t size;
  {{$union.Name}}_Tag tag;
  
  union {
    {{range $field := $union.Fields -}}
    {{$field.Text}};
    {{end}}
    uint64_t unknown;
  } data;
};
{{/* TODO(vardhan): The following will no longer be true if the largest mojom
     type is > 8 bytes. */}}
MOJO_STATIC_ASSERT(sizeof(struct {{$union.Name}}) == 16,
             "struct {{$union.Name}} must be 16 bytes");
{{end}}
`
