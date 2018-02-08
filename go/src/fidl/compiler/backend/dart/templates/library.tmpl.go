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

{{ range $enum := .Enums -}}
{{ template "EnumDeclaration" $enum }}
{{ end -}}
{{- end -}}
`
