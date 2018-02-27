// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const Header = `
{{- define "GenerateHeaderPreamble" -}}
// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/fidl/cpp/internal/header.h"

namespace {{ .Namespace }} {
{{ end }}

{{- define "GenerateHeaderPostamble" -}}
}  // namespace {{ .Namespace }}
{{ end }}

{{- define "GenerateTraitsPreamble" -}}
namespace fidl {
{{ end }}

{{- define "GenerateTraitsPostamble" -}}
}  // namespace fidl
{{ end }}
`
