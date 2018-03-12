// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const Library = `
{{- define "GenerateLibraryFile" -}}
// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import _zx "syscall/zx"

{{ range $enum := .Enums -}}
{{ template "EnumDefinition" $enum }}
{{ end -}}
{{ range $struct := .Structs -}}
{{ template "StructDefinition" $struct }}
{{ end -}}

{{- end -}}
`
