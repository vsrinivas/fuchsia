// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const GenerateInterfaceDeclarations = `
{{- /* . (dot) refers to the Go type |cgen.InterfaceTemplate| */ -}}
{{define "GenerateInterfaceDeclarations" -}}
{{- $interface := . -}}
// --- {{$interface.Name}} ---
#define {{$interface.Name}}__ServiceName ((const char*)"{{$interface.ServiceName}}")
#define {{$interface.Name}}__CurrentVersion ((uint32_t){{$interface.Version}})

// Enums
{{range $enum := $interface.Enums -}}
{{template "GenerateEnum" $enum}}
{{end -}}

// Constants
{{range $const := $interface.Constants -}}
extern const {{$const.Type}} {{$const.Name}};
{{end}}

{{range $message := $interface.Messages -}}
// Message: {{$message.Name}}

#define {{$interface.Name}}_{{$message.Name}}__Ordinal \
    ((uint32_t){{$message.MessageOrdinal}})
#define {{$interface.Name}}_{{$message.Name}}__MinVersion \
    ((uint32_t){{$message.MinVersion}})

struct {{$message.RequestStruct.Name}};
{{template "GenerateStructDeclarations" $message.RequestStruct}}
{{if ne $message.ResponseStruct.Name "" -}}
struct {{$message.ResponseStruct.Name}};
{{template "GenerateStructDeclarations" $message.ResponseStruct}}
{{end}}
{{end}}
{{end}}
`

const GenerateInterfaceDefinitions = `
{{- /* . (dot) refers to the Go type |cgen.InterfaceTemplate| */ -}}
{{define "GenerateInterfaceDefinitions" -}}
{{- $interface := . -}}
{{range $const := $interface.Constants -}}
const {{$const.Type}} {{$const.Name}} = {{$const.Value}};
{{end -}}

// Interface message struct definitions:
{{range $message := $interface.Messages -}}
// Message: {{$message.Name}}

struct {{$message.RequestStruct.Name}};
{{template "GenerateStructDefinitions" $message.RequestStruct}}
{{if ne $message.ResponseStruct.Name "" -}}
struct {{$message.ResponseStruct.Name}};
{{template "GenerateStructDefinitions" $message.ResponseStruct}}
{{end}}

{{end}}
{{end}}
`
