// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const tmplDecoderEncoder = `
{{- define "DecoderEncoder" -}}
::fidl::fuzzing::DecoderEncoderForType{
	.fidl_type_name = "{{ .Wire }}",
	.has_flexible_envelope = {{ .TypeShapeV1.HasFlexibleEnvelope }},
	.decoder_encoder = ::fidl::fuzzing::DecoderEncoderImpl<{{ .Wire }}>,
},
{{- end -}}
`
