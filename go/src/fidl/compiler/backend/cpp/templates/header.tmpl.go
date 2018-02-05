// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const Header = `
{{- define "GenerateHeaderFile" -}}
// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/bindings2/internal/header.h"

{{ range $enum := .Enums -}}
{{ template "EnumDeclaration" $enum }}
{{ end -}}
{{ range $union := .Unions -}}
{{ template "UnionForwardDeclaration" $union }}
{{ end -}}
{{ range $struct := .Structs -}}
{{ template "StructForwardDeclaration" $struct }}
{{ end -}}
{{ range $interface := .Interfaces -}}
{{ template "InterfaceForwardDeclaration" $interface }}
{{ end -}}
{{ range $union := .Unions -}}
{{ template "UnionDeclaration" $union }}
{{ end -}}
{{ range $struct := .Structs -}}
{{ template "StructDeclaration" $struct }}
{{ end -}}
{{ range $interface := .Interfaces }}
{{ template "InterfaceDeclaration" $interface }}
{{ end -}}
{{- end -}}
`
