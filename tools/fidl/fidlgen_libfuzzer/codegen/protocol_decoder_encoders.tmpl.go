// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const tmplProtocolDecoderEncoders = `
{{- define "ProtocolDecoderEncoders" -}}

{{- range .Methods -}}

{{- if .HasRequest }}
::fidl::fuzzing::DecoderEncoderForType{
	.fidl_type_name = "{{ .WireRequest }}",
	.has_flexible_envelope = {{ .Request.TypeShapeV1.HasFlexibleEnvelope }},
	.decoder_encoder = ::fidl::fuzzing::DecoderEncoderImpl<{{ .WireRequest }}>,
},
{{- end -}}

{{- if .HasResponse }}
::fidl::fuzzing::DecoderEncoderForType{
	.fidl_type_name = "{{ .WireResponse }}",
	.has_flexible_envelope = {{ .Response.TypeShapeV1.HasFlexibleEnvelope }},
	.decoder_encoder = ::fidl::fuzzing::DecoderEncoderImpl<{{ .WireResponse }}>,
},
{{- end -}}

{{- end -}}

{{- end -}}
`
