// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fragments

const Bits = `
{{- define "BitsForwardDeclaration" }}
enum class {{ .Name }} : {{ .Type }} {
  {{- range .Members }}
  {{ .Name }} = {{ .Value }},
  {{- end }}
};

{{ end }}

`
