// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentSendEventCallerAllocateTmpl = `
{{- define "SendEventCallerAllocateMethodSignature" -}}
Send{{ .Name }}Event(::zx::unowned_channel _channel, ::fidl::BytePart _buffer, {{ template "Params" .Response }})
{{- end }}

{{- define "SendEventCallerAllocateMethodDefinition" }}
zx_status_t {{ .LLProps.ProtocolName }}::{{ template "SendEventCallerAllocateMethodSignature" . }} {
  {{ .Name }}Response::UnownedOutgoingMessage _response(_buffer.data(), _buffer.capacity()
  {{- template "CommaPassthroughMessageParams" .Response -}}
  );
  _response.Write(_channel->get());
  return _response.status();
}
{{- end }}
`
