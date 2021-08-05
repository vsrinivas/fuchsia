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
		 {{ RenderParams (printf "::fidl::UnownedClientEnd<%s> _client" .Protocol)
		                       .RequestArgs }});
   {{- if .HasResponse }}
	 {{ .WireResult.Self }}(
		{{ RenderParams (printf "::fidl::UnownedClientEnd<%s> _client" .Protocol)
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
	   {{ .Response.ClientAllocationV1.BackingBufferType }} bytes_;
	 {{- end }}
};
{{- end }}






{{- define "MethodResultDefinition" }}
{{- IfdefFuchsia -}}
{{- EnsureNamespace "" }}
{{ .WireResult }}::{{ .WireResult.Self }}(
    {{- RenderParams (printf "::fidl::UnownedClientEnd<%s> _client" .Protocol)
                          .RequestArgs }})
   {
  ::fidl::OwnedEncodedMessage<{{ .WireRequest }}> _request(
      {{- RenderForwardParams "::fidl::internal::AllowUnownedInputRef{}" "zx_txid_t(0)" .RequestArgs }});
  auto& _outgoing = _request.GetOutgoingMessage();
  {{- if .HasResponse }}
  _outgoing.Call<{{ .WireResponse }}>(
      _client,
      bytes_.data(),
      bytes_.size());
  {{- else }}
  _outgoing.Write(_client);
  {{- end }}
  SetResult(_outgoing);
}
  {{- if .HasResponse }}

{{ .WireResult }}::{{ .WireResult.Self }}(
    {{- RenderParams (printf "::fidl::UnownedClientEnd<%s> _client" .Protocol)
                           .RequestArgs "zx_time_t _deadline" }})
   {
  ::fidl::OwnedEncodedMessage<{{ .WireRequest }}> _request(
	  {{- RenderForwardParams "::fidl::internal::AllowUnownedInputRef{}" "zx_txid_t(0)" .RequestArgs }});
  auto& _outgoing = _request.GetOutgoingMessage();
  _outgoing.Call<{{ .WireResponse }}>(
      _client,
      bytes_.data(),
      bytes_.size(),
      _deadline);
  SetResult(_outgoing);
}
  {{- end }}
{{- EndifFuchsia -}}
{{- end }}
`
