// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentSendEventCFlavorTmpl = `
{{- define "SendEventCFlavorMethodSignature" -}}
Send{{ .Name }}Event(::zx::unowned_channel _chan {{- if .Response }}, {{ end }}{{ template "Params" .Response }})
{{- end }}

{{- define "SendEventCFlavorMethodDefinition" }}
zx_status_t {{ .LLProps.ProtocolName }}::{{ template "SendEventCFlavorMethodSignature" . }} {
  {{- if .LLProps.LinearizeResponse }}
  {{/* tracking_ptr destructors will be called when _response goes out of scope */}}
  {{ .Name }}Response _response{
  {{- template "PassthroughMessageParams" .Response -}}
  };
  {{- else }}
  {{/* tracking_ptrs won't free allocated memory because destructors aren't called.
  This is ok because there are no tracking_ptrs, since LinearizeResponse is true when
  there are pointers in the object. */}}
  // Destructors can't be called because it will lead to handle double close
  // (here and in fidl::Encode).
  FIDL_ALIGNDECL uint8_t _response_buffer[sizeof({{ .Name }}Response)];
  auto& _response = *new (_response_buffer) {{ .Name }}Response{
  {{- template "PassthroughMessageParams" .Response -}}
  };
  {{- end }}

  auto _encoded = ::fidl::internal::LinearizedAndEncoded<{{ .Name }}Response>(&_response);
  auto& _encode_result = _encoded.result();
  if (_encode_result.status != ZX_OK) {
    return _encode_result.status;
  }
  return ::fidl::Write(::zx::unowned_channel(_chan), std::move(_encode_result.message));
}
{{- end }}
`
