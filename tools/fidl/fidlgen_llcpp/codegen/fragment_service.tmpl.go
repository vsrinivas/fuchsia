// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const fragmentServiceTmpl = `
{{- define "ServiceForwardDeclaration" }}
class {{ .Name }};
{{- end }}

{{- define "ServiceDeclaration" }}
{{ "" }}
{{- range .DocComments }}
//{{ . }}
{{- end }}
class {{ .Name }} final {
  {{ .Name }}() = default;
 public:
  static constexpr char Name[] = "{{ .ServiceName }}";

  // Client protocol for connecting to member protocols of a service instance.
  class ServiceClient final {
    ServiceClient() = delete;
   public:
    ServiceClient(::zx::channel dir, ::fidl::internal::ConnectMemberFunc connect_func)
    : dir_(std::move(dir)), connect_func_(connect_func) {}
    {{- range .Members }}
  {{ "" }}
    // Connects to the member protocol "{{ .Name }}". Returns a |fidl::ClientChannel| on
    // success, which can be used with |fidl::BindSyncClient| to create a synchronous
    // client.
    //
    // # Errors
    //
    // On failure, returns a fit::error with zx_status_t != ZX_OK.
    // Failures can occur if channel creation failed, or if there was an issue making
    // a |fuchsia.io.Directory::Open| call.
    //
    // Since the call to |Open| is asynchronous, an error sent by the remote end will not
    // result in a failure of this method. Any errors sent by the remote will appear on
    // the |ClientChannel| returned from this method.
    ::fidl::result<::fidl::ClientChannel<{{ .ProtocolType }}>> connect_{{ .Name }}() {
      ::zx::channel local, remote;
      zx_status_t result = ::zx::channel::create(0, &local, &remote);
      if (result != ZX_OK) {
        return ::fit::error(result);
      }
      result =
          connect_func_(::zx::unowned_channel(dir_), ::fidl::StringView("{{ .Name }}"), std::move(remote));
      if (result != ZX_OK) {
        return ::fit::error(result);
      }
      return ::fit::ok(::fidl::ClientChannel<{{ .ProtocolType}}>(std::move(local)));
    }
    {{- end }}

   private:
    ::zx::channel dir_;
    ::fidl::internal::ConnectMemberFunc connect_func_;
  };

  // Facilitates member protocol registration for servers.
  class Handler final {
   public:
    // Constructs a FIDL Service-typed handler. Does not take ownership of |service_handler|.
    explicit Handler(::llcpp::fidl::ServiceHandlerInterface* service_handler)
        : service_handler_(service_handler) {}

    {{- range .Members }}
    {{ "" }}
    // Adds member "{{ .Name }}" to the service instance. |handler| will be invoked on connection
    // attempts.
    //
    // # Errors
    //
    // Returns ZX_ERR_ALREADY_EXISTS if the member was already added.
    zx_status_t add_{{ .Name }}(::llcpp::fidl::ServiceHandlerInterface::MemberHandler handler) {
      return service_handler_->AddMember("{{ .Name }}", std::move(handler));
    }
    {{- end }}

   private:
    ::llcpp::fidl::ServiceHandlerInterface* service_handler_;  // Not owned.
  };
};
{{- end }}
`
