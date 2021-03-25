// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentSyncRequestManagedTmpl = `
{{- define "SyncRequestManagedMethodDefinition" }}
#ifdef __Fuchsia__
{{ .Protocol }}::ResultOf::{{ .Name }}::{{ .Name }}(
    ::fidl::UnownedClientEnd<{{ .Protocol }}> _client
    {{- .RequestArgs | CommaMessagePrototype }})
   {
  ::fidl::OwnedEncodedMessage<{{ .WireRequest }}> _request(zx_txid_t(0)
    {{- .RequestArgs | CommaParamNames -}});
  {{- if .HasResponse }}
  _request.GetOutgoingMessage().Call<{{ .WireResponse }}>(
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

{{ .Protocol }}::ResultOf::{{ .Name }}::{{ .Name }}(
    ::fidl::UnownedClientEnd<{{ .Protocol }}> _client
    {{- .RequestArgs | CommaMessagePrototype -}}
    , zx_time_t _deadline)
   {
  ::fidl::OwnedEncodedMessage<{{ .WireRequest }}> _request(zx_txid_t(0)
    {{- .RequestArgs | CommaParamNames -}});
  _request.GetOutgoingMessage().Call<{{ .WireResponse }}>(
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
