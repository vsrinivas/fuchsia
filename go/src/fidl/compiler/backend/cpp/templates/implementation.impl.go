// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const Implementation = `
{{- define "GenerateImplementationPreamble" -}}
// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "{{ .PrimaryHeader }}"

#include "lib/fidl/cpp/internal/implementation.h"
{{ end }}
{{- define "GenerateImplementationPostamble" -}}
{{ end }}
`
