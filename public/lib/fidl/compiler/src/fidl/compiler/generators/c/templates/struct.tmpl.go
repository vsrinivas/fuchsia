// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const GenerateStructDeclarations = `
{{- /* . (dot) refers to the Go type |cgen.StructTemplate| */ -}}
{{define "GenerateStructDeclarations" -}}
{{- $struct := . -}}

// -- {{$struct.Name}} --
// Enums
{{range $enum := $struct.Enums -}}
{{template "GenerateEnum" $enum}}
{{end -}}

// Constants
{{range $const := $struct.Constants -}}
extern const {{$const.Type}} {{$const.Name}};
{{end -}}

// Struct definition
struct {{$struct.Name}} {
  struct MojomStructHeader header_;
  {{range $field := $struct.Fields -}}
  {{$field.Text}};  // {{$field.Comment}}
  {{end}}
};

struct {{$struct.Name}}* {{$struct.Name}}_New(struct MojomBuffer* in_buffer);

void {{$struct.Name}}_Init(
  struct {{$struct.Name}}* in_data);

void {{$struct.Name}}_CloseAllHandles(
  struct {{$struct.Name}}* in_data);

struct {{$struct.Name}}* {{$struct.Name}}_DeepCopy(
  struct MojomBuffer* in_buffer,
  struct {{$struct.Name}}* in_struct);

size_t {{$struct.Name}}_ComputeSerializedSize(
  const struct {{$struct.Name}}* in_data);

void {{$struct.Name}}_EncodePointersAndHandles(
  struct {{$struct.Name}}* inout_struct, uint32_t in_struct_size,
  struct MojomHandleBuffer* inout_handle_buffer);

void {{$struct.Name}}_DecodePointersAndHandles(
  struct {{$struct.Name}}* inout_struct, uint32_t in_struct_size,
  MojoHandle inout_handles[], uint32_t in_num_handles);

MojomValidationResult {{$struct.Name}}_Validate(
  const struct {{$struct.Name}}* in_struct, uint32_t in_struct_size,
  uint32_t in_num_handles);

{{end}}
`

// TODO(vardhan): Move other struct constant definitions in here.
const GenerateStructDefinitions = `
{{- /* . (dot) refers to the Go type |cgen.StructTemplate| */ -}}
{{define "GenerateStructDefinitions" -}}
{{- $struct := . -}}

{{range $const := $struct.Constants -}}
const {{$const.Type}} {{$const.Name}} = {{$const.Value}};
{{end -}}

struct {{$struct.Name}}* {{$struct.Name}}_DeepCopy(
  struct MojomBuffer* in_buffer,
  struct {{$struct.Name}}* in_struct) {
  struct {{$struct.Name}}* out_struct = NULL;
  if (!MojomStruct_DeepCopy(
      in_buffer, &{{$struct.Name}}__TypeDesc,
      (struct MojomStructHeader*)in_struct,
      (struct MojomStructHeader**)&out_struct)) {
    return NULL;
  }
  return out_struct;
}

size_t {{$struct.Name}}_ComputeSerializedSize(
    const struct {{$struct.Name}}* in_data) {
  return MojomStruct_ComputeSerializedSize(
    &{{$struct.Name}}__TypeDesc,
		(const struct MojomStructHeader*)in_data);
}

void {{$struct.Name}}_EncodePointersAndHandles(
  struct {{$struct.Name}}* inout_struct, uint32_t in_struct_size,
  struct MojomHandleBuffer* inout_handle_buffer) {
  MojomStruct_EncodePointersAndHandles(
    &{{$struct.Name}}__TypeDesc,
    (struct MojomStructHeader*)inout_struct, in_struct_size,
    inout_handle_buffer);
}

void {{$struct.Name}}_DecodePointersAndHandles(
  struct {{$struct.Name}}* inout_struct, uint32_t in_struct_size,
  MojoHandle inout_handles[], uint32_t in_num_handles) {
  MojomStruct_DecodePointersAndHandles(
    &{{$struct.Name}}__TypeDesc,
    (struct MojomStructHeader*)inout_struct, in_struct_size,
    inout_handles, in_num_handles);
}

MojomValidationResult {{$struct.Name}}_Validate(
  const struct {{$struct.Name}}* in_struct, uint32_t in_struct_size,
  uint32_t in_num_handles) {
  struct MojomValidationContext context = {
    .next_handle_index = 0,
    .next_pointer = (char*)in_struct + in_struct->header_.num_bytes,
  };
  return MojomStruct_Validate(
    &{{$struct.Name}}__TypeDesc,
    (struct MojomStructHeader*)in_struct, in_struct_size, in_num_handles,
    &context);
}

{{end}}
`
