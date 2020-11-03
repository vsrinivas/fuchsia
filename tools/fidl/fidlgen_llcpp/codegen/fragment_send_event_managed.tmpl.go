// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentSendEventManagedTmpl = `
{{- define "SendEventManagedMethodSignature" -}}
Send{{ .Name }}Event(::zx::unowned_channel _channel {{- if .Response }}, {{ end }}{{ template "Params" .Response }})
{{- end }}

{{- define "SendEventManagedMethodDefinition" }}
zx_status_t {{ .LLProps.ProtocolName }}::{{ template "SendEventManagedMethodSignature" . }} {
  {{ .Name }}Response::OwnedOutgoingMessage _response{
  {{- template "PassthroughMessageParams" .Response -}}
  };
  _response.Write(_channel->get());
  return _response.status();
}
{{- end }}
`
