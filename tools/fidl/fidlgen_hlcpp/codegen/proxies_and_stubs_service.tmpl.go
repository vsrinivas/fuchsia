// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const serviceTemplate = `
{{- define "ServiceForwardDeclaration" }}
#ifdef __Fuchsia__
class {{ .Name }};
#endif // __Fuchsia__
{{- end }}

{{- define "ServiceDeclaration" }}
#ifdef __Fuchsia__
{{range .DocComments}}
///{{ . }}
{{- end}}
class {{ .Name }} final {
 public:
  class Handler;

  static constexpr char Name[] = "{{ .ServiceName }}";

  explicit {{ .Name }}(std::unique_ptr<::fidl::ServiceConnector> service)
      : service_(std::move(service)) {}

  explicit operator bool() const { return !!service_; }

  {{- range .Members }}
  /// Returns a |fidl::MemberConnector| which can be used to connect to the member protocol "{{ .Name }}".
  ::fidl::MemberConnector<{{ .ProtocolType }}> {{ .MethodName }}() const {
    return ::fidl::MemberConnector<{{ .ProtocolType }}>(service_.get(), "{{ .Name }}");
  }
  {{- end }}

 private:
  std::unique_ptr<::fidl::ServiceConnector> service_;
};

/// Facilitates member protocol registration for servers.
class {{ .Name }}::Handler final {
 public:
  /// Constructs a new |Handler|. Does not take ownership of |service|.
  explicit Handler(::fidl::ServiceHandlerBase* service)
  {{- with .Members }}
      : service_(service) {}
  {{- else }}
      { (void)service; }
  {{- end }}

  {{- range .Members }}
  /// Adds member "{{ .Name }}" to the service instance. |handler| is invoked when a connection
  /// is made to the member protocol.
  ///
  /// # Errors
  ///
  /// Returns ZX_ERR_ALREADY_EXISTS if the member was already added.
  zx_status_t add_{{ .Name }}(::fidl::InterfaceRequestHandler<{{ .ProtocolType }}> handler) {
    return service_->AddMember("{{ .Name }}", std::move(handler));
  }
  {{- end }}

 private:
  {{- with .Members }}
  ::fidl::ServiceHandlerBase* const service_;
  {{- end }}
};
#endif // __Fuchsia__
{{- end }}
`
