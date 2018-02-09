// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const Library = `
{{- define "GenerateLibraryFile" -}}
// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:lib.fidl.dart/bindings.dart' as bindings;
import 'package:zircon/zircon.dart';

{{ range $enum := .Enums -}}
{{ template "EnumDeclaration" $enum }}
{{ end -}}
{{ range $struct := .Structs -}}
{{ template "StructDeclaration" $struct }}
{{ end -}}
{{ range $union := .Unions -}}
{{ template "UnionDeclaration" $union }}
{{ end -}}
{{- end -}}
`
