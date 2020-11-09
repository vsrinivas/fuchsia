// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const handleMetadataWrapperTmpl = `
{{- define "HandleMetadataWrapperDeclaration" -}}
wrap_handle_metadata!({{ .Name }}, {{ .Subtype }}, {{ .Rights }});
{{ end }}`
