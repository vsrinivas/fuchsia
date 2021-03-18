// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentSyncRequestManagedTmpl = `
{{- define "SyncRequestManagedMethodDefinition" }}
#ifdef __Fuchsia__
{{ .LLProps.ProtocolName }}::ResultOf::{{ .Name }}::{{ .Name }}(
    ::fidl::UnownedClientEnd<{{ .LLProps.ProtocolName }}> _client
    {{- .RequestArgs | CommaMessagePrototype }})
   {
  ::fidl::OwnedEncodedMessage<{{ .Name }}Request> _request(zx_txid_t(0)
    {{- .RequestArgs | CommaParamNames -}});
  {{- if .HasResponse }}
  _request.GetOutgoingMessage().Call<{{ .Name }}Response>(
      _client,
      bytes_.data(),
      bytes_.size());
  {{- else }}
  _request.GetOutgoingMessage().Write(_client);
  {{- end }}
  status_ = _request.status();
  error_ = _request.error();
}
  {{- if .HasResponse }}

{{ .LLProps.ProtocolName }}::ResultOf::{{ .Name }}::{{ .Name }}(
    ::fidl::UnownedClientEnd<{{ .LLProps.ProtocolName }}> _client
    {{- .RequestArgs | CommaMessagePrototype -}}
    , zx_time_t _deadline)
   {
  ::fidl::OwnedEncodedMessage<{{ .Name }}Request> _request(zx_txid_t(0)
    {{- .RequestArgs | CommaParamNames -}});
  _request.GetOutgoingMessage().Call<{{ .Name }}Response>(
      _client,
      bytes_.data(),
      bytes_.size(),
      _deadline);
  status_ = _request.status();
  error_ = _request.error();
}
  {{- end }}
#endif
{{- end }}
`
