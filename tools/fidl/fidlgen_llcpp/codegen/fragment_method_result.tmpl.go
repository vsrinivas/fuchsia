// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

// fragmentMethodResultTmpl contains the definition for
// fidl::WireResult<Method>.
const fragmentMethodResultTmpl = `
{{- define "MethodResultDeclaration" }}
{{- EnsureNamespace "" }}
template<>
class {{ .WireResult }} final : public ::fidl::Result {
	public:
	 explicit {{ .WireResult.Self }}(
		 {{ RenderCalleeParams (printf "::fidl::UnownedClientEnd<%s> _client" .Protocol)
		                       .RequestArgs }});
   {{- if .HasResponse }}
	 {{ .WireResult.Self }}(
		{{ RenderCalleeParams (printf "::fidl::UnownedClientEnd<%s> _client" .Protocol)
		                      .RequestArgs "zx_time_t _deadline"}});
   {{- end }}
	 explicit {{ .WireResult.Self }}(const ::fidl::Result& result) : ::fidl::Result(result) {}
	 {{ .WireResult.Self }}({{ .WireResult.Self }}&&) = delete;
	 {{ .WireResult.Self }}(const {{ .WireResult.Self }}&) = delete;
	 {{ .WireResult.Self }}* operator=({{ .WireResult.Self }}&&) = delete;
	 {{ .WireResult.Self }}* operator=(const {{ .WireResult.Self }}&) = delete;
	 {{- if and .HasResponse .Response.IsResource }}
	 ~{{ .WireResult.Self }}() {
	   if (ok()) {
		 Unwrap()->_CloseHandles();
	   }
	 }
	 {{- else }}
	 ~{{ .WireResult.Self }}() = default;
	 {{- end }}
	 {{- if .HasResponse }}

	 {{ .WireResponse }}* Unwrap() {
	   ZX_DEBUG_ASSERT(ok());
	   return reinterpret_cast<{{ .WireResponse }}*>(bytes_.data());
	 }
	 const {{ .WireResponse }}* Unwrap() const {
	   ZX_DEBUG_ASSERT(ok());
	   return reinterpret_cast<const {{ .WireResponse }}*>(bytes_.data());
	 }

	 {{ .WireResponse }}& value() { return *Unwrap(); }
	 const {{ .WireResponse }}& value() const { return *Unwrap(); }

	 {{ .WireResponse }}* operator->() { return &value(); }
	 const {{ .WireResponse }}* operator->() const { return &value(); }

	 {{ .WireResponse }}& operator*() { return value(); }
	 const {{ .WireResponse }}& operator*() const { return value(); }
	 {{- end }}

	private:
	 {{- if .HasResponse }}
	   {{ .Response.ClientAllocation.BackingBufferType }} bytes_;
	 {{- end }}
};
{{- end }}






{{- define "MethodResultDefinition" }}
{{- IfdefFuchsia -}}
{{- EnsureNamespace "" }}
{{ .WireResult }}::{{ .WireResult.Self }}(
    {{- RenderCalleeParams (printf "::fidl::UnownedClientEnd<%s> _client" .Protocol)
                          .RequestArgs }})
   {
  ::fidl::OwnedEncodedMessage<{{ .WireRequest }}> _request(
	  {{- RenderForwardParams "zx_txid_t(0)" .RequestArgs }});
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

{{ .WireResult }}::{{ .WireResult.Self }}(
    {{- RenderCalleeParams (printf "::fidl::UnownedClientEnd<%s> _client" .Protocol)
                           .RequestArgs "zx_time_t _deadline" }})
   {
  ::fidl::OwnedEncodedMessage<{{ .WireRequest }}> _request(
	  {{- RenderForwardParams "zx_txid_t(0)" .RequestArgs }});
  _request.GetOutgoingMessage().Call<{{ .WireResponse }}>(
      _client,
      bytes_.data(),
      bytes_.size(),
      _deadline);
  status_ = _request.status();
  error_ = _request.error();
}
  {{- end }}
{{- EndifFuchsia -}}
{{- end }}
`
