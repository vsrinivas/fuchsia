// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const Implementation = `
{{- define "GenerateImplementationFile" -}}
// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "{{ .PrimaryHeader }}"

#include "lib/fidl/cpp/bindings2/internal/implementation.h"

{{ range $interface := .Interfaces }}
{{ template "InterfaceDefinition" $interface }}
{{ end }}
{{- end -}}
`
