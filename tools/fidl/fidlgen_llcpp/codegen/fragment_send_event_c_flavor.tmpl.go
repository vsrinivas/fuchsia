// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentSendEventCFlavorTmpl = `
{{- define "SendEventCFlavorMethodSignature" -}}
Send{{ .Name }}Event(::zx::unowned_channel _channel {{- if .Response }}, {{ end }}{{ template "Params" .Response }})
{{- end }}

{{- define "SendEventCFlavorMethodDefinition" }}
zx_status_t {{ .LLProps.ProtocolName }}::{{ template "SendEventCFlavorMethodSignature" . }} {
  {{ .Name }}OwnedResponse _response{
  {{- template "PassthroughMessageParams" .Response -}}
  };
  _response.Write(_channel->get());
  return _response.status();
}
{{- end }}
`
