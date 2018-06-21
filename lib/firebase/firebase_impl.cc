// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/firebase/firebase_impl.h"

#include <memory>
#include <sstream>
#include <utility>

#include "lib/fsl/vmo/strings.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/ascii.h"
#include "lib/fxl/strings/join_strings.h"
#include "peridot/lib/socket/socket_drainer_client.h"

namespace firebase {

namespace http = ::fuchsia::net::oldhttp;

namespace {

std::function<http::URLRequest()> MakeRequest(const std::string& url,
                                              const std::string& method,
                                              const std::string& message,
                                              bool stream_request = false) {
  fsl::SizedVmo body;
  if (!message.empty()) {
    if (!fsl::VmoFromString(message, &body)) {
      FXL_LOG(ERROR) << "Unable to create VMO from string.";
      return nullptr;
    }
  }

  return fxl::MakeCopyable([url, method, body = std::move(body),
                            stream_request]() {
    http::URLRequest request;
    request.url = url;
    request.method = method;
    request.auto_follow_redirects = true;
    if (body) {
      fsl::SizedVmo duplicated_body;
      zx_status_t status =
          body.Duplicate(ZX_RIGHTS_BASIC | ZX_RIGHT_READ, &duplicated_body);
      if (status != ZX_OK) {
        FXL_LOG(WARNING) << "Unable to duplicate a vmo. Status: " << status;
        return http::URLRequest();
      }
      request.body = http::URLBody::New();
      request.body->set_sized_buffer(std::move(duplicated_body).ToTransport());
    }
    if (stream_request) {
      http::HttpHeader accept_header;
      accept_header.name = "Accept";
      accept_header.value = "text/event-stream";
      request.headers.push_back(std::move(accept_header));
    }
    return request;
  });
}

}  // namespace

struct FirebaseImpl::WatchData {
  WatchData();
  ~WatchData();

  callback::AutoCancel request;
  std::unique_ptr<EventStream> event_stream;
  std::unique_ptr<socket::SocketDrainerClient> drainer;
};

FirebaseImpl::WatchData::WatchData() {}
FirebaseImpl::WatchData::~WatchData() {}

FirebaseImpl::FirebaseImpl(network_wrapper::NetworkWrapper* network_wrapper,
                           const std::string& db_id, const std::string& prefix)
    : network_wrapper_(network_wrapper), api_url_(BuildApiUrl(db_id, prefix)) {
  FXL_DCHECK(network_wrapper_);
}

FirebaseImpl::~FirebaseImpl() {}

void FirebaseImpl::Get(
    const std::string& key, const std::vector<std::string>& query_params,
    std::function<void(Status status, std::unique_ptr<rapidjson::Value> value)>
        callback) {
  auto request_callback = [callback = std::move(callback)](
                              Status status, const std::string& response) {
    if (status != Status::OK) {
      callback(status, std::make_unique<rapidjson::Value>());
      return;
    }

    auto document = std::make_unique<rapidjson::Document>();
    document->Parse(response.c_str(), response.size());
    if (document->HasParseError()) {
      callback(Status::PARSE_ERROR, std::make_unique<rapidjson::Value>());
      return;
    }

    callback(Status::OK, std::move(document));
  };

  Request(BuildRequestUrl(key, query_params), "GET", "", request_callback);
}

void FirebaseImpl::Put(const std::string& key,
                       const std::vector<std::string>& query_params,
                       const std::string& data,
                       std::function<void(Status status)> callback) {
  Request(BuildRequestUrl(key, query_params), "PUT", data,
          [callback = std::move(callback)](Status status,
                                           const std::string& response) {
            // Ignore the response body, which is the same data we sent to the
            // server.
            callback(status);
          });
}

void FirebaseImpl::Patch(const std::string& key,
                         const std::vector<std::string>& query_params,
                         const std::string& data,
                         std::function<void(Status status)> callback) {
  Request(BuildRequestUrl(key, query_params), "PATCH", data,
          [callback = std::move(callback)](Status status,
                                           const std::string& response) {
            // Ignore the response body, which is the same data we sent to the
            // server.
            callback(status);
          });
}

void FirebaseImpl::Delete(const std::string& key,
                          const std::vector<std::string>& query_params,
                          std::function<void(Status status)> callback) {
  Request(
      BuildRequestUrl(key, query_params), "DELETE", "",
      [callback = std::move(callback)](
          Status status, const std::string& response) { callback(status); });
}

void FirebaseImpl::Watch(const std::string& key,
                         const std::vector<std::string>& query_params,
                         WatchClient* watch_client) {
  watch_data_[watch_client] = std::make_unique<WatchData>();
  watch_data_[watch_client]->request.Reset(network_wrapper_->Request(
      MakeRequest(BuildRequestUrl(key, query_params), "GET", "", true),
      [this, watch_client](http::URLResponse response) {
        OnStream(watch_client, std::move(response));
      }));
}

void FirebaseImpl::UnWatch(WatchClient* watch_client) {
  watch_data_.erase(watch_client);
}

std::string FirebaseImpl::BuildApiUrl(const std::string& db_id,
                                      const std::string& prefix) {
  std::string api_url = "https://" + db_id + ".firebaseio.com";

  if (!prefix.empty()) {
    FXL_DCHECK(prefix.front() != '/');
    FXL_DCHECK(prefix.back() != '/');
    api_url.append("/");
    api_url.append(prefix);
  }

  FXL_DCHECK(api_url.back() != '/');
  return api_url;
}

std::string FirebaseImpl::BuildRequestUrl(
    const std::string& key,
    const std::vector<std::string>& query_params) const {
  std::ostringstream result;
  result << api_url_;
  result << "/" << key << ".json";
  if (query_params.empty()) {
    return result.str();
  }
  result << "?" << fxl::JoinStrings(query_params, "&");
  return result.str();
}

void FirebaseImpl::Request(
    const std::string& url, const std::string& method,
    const std::string& message,
    const std::function<void(Status status, std::string response)>& callback) {
  requests_.emplace(
      network_wrapper_->Request(MakeRequest(url, method, message),
                                [this, callback](http::URLResponse response) {
                                  OnResponse(callback, std::move(response));
                                }));
}

void FirebaseImpl::OnResponse(
    const std::function<void(Status status, std::string response)>& callback,
    http::URLResponse response) {
  if (response.error) {
    FXL_LOG(ERROR) << response.url << " error " << response.error->description;
    callback(Status::NETWORK_ERROR, "");
    return;
  }

  if (response.status_code != 200 && response.status_code != 204) {
    const std::string& url = response.url;
    const std::string& status_line = response.status_line;
    FXL_DCHECK(response.body->is_stream());
    auto& drainer = drainers_.emplace();
    drainer.Start(std::move(response.body->stream()),
                  [callback, url, status_line](const std::string& body) {
                    FXL_LOG(ERROR)
                        << url << " error " << status_line << ":" << std::endl
                        << body;
                    callback(Status::SERVER_ERROR, "");
                  });
    return;
  }

  FXL_DCHECK(response.body->is_stream());
  auto& drainer = drainers_.emplace();
  drainer.Start(
      std::move(response.body->stream()),
      [callback](const std::string& body) { callback(Status::OK, body); });
}

void FirebaseImpl::OnStream(WatchClient* watch_client,
                            http::URLResponse response) {
  if (response.error) {
    FXL_LOG(ERROR) << response.url << " error " << response.error->description;
    watch_client->OnConnectionError();
    watch_data_.erase(watch_client);
    return;
  }

  FXL_DCHECK(response.body->is_stream());

  if (response.status_code != 200 && response.status_code != 204) {
    const std::string& url = response.url;
    const std::string& status_line = response.status_line;
    watch_data_[watch_client]->drainer =
        std::make_unique<socket::SocketDrainerClient>();
    watch_data_[watch_client]->drainer->Start(
        std::move(response.body->stream()),
        [this, watch_client, url, status_line](const std::string& body) {
          FXL_LOG(ERROR) << url << " error " << status_line << ":" << std::endl
                         << body;
          watch_client->OnConnectionError();
          watch_data_.erase(watch_client);
        });
    return;
  }

  watch_data_[watch_client]->event_stream = std::make_unique<EventStream>();
  watch_data_[watch_client]->event_stream->Start(
      std::move(response.body->stream()),
      [this, watch_client](Status status, const std::string& event,
                           const std::string& data) {
        OnStreamEvent(watch_client, status, event, data);
      },
      [this, watch_client]() { OnStreamComplete(watch_client); });
}

void FirebaseImpl::OnStreamComplete(WatchClient* watch_client) {
  watch_data_[watch_client]->event_stream.reset();
  watch_client->OnConnectionError();
  watch_data_.erase(watch_client);
}

void FirebaseImpl::OnStreamEvent(WatchClient* watch_client, Status /*status*/,
                                 const std::string& event,
                                 const std::string& payload) {
  if (event == "put" || event == "patch") {
    rapidjson::Document parsed_payload;
    parsed_payload.Parse(payload.c_str(), payload.size());
    if (parsed_payload.HasParseError()) {
      HandleMalformedEvent(watch_client, event, payload,
                           "failed to parse the event payload");
      return;
    }

    // Both 'put' and 'patch' events must carry a dictionary of "path" and
    // "data".
    if (!parsed_payload.IsObject()) {
      HandleMalformedEvent(watch_client, event, payload,
                           "event payload doesn't appear to be an object");
      return;
    }
    if (!parsed_payload.HasMember("path") ||
        !parsed_payload["path"].IsString()) {
      HandleMalformedEvent(watch_client, event, payload,
                           "event payload doesn't contain the `path` string");
      return;
    }
    if (!parsed_payload.HasMember("data")) {
      HandleMalformedEvent(watch_client, event, payload,
                           "event payload doesn't contain the `data` member");
      return;
    }

    if (event == "put") {
      watch_client->OnPut(parsed_payload["path"].GetString(),
                          parsed_payload["data"]);
    } else if (event == "patch") {
      // In case of patch, data must be a dictionary itself.
      if (!parsed_payload["data"].IsObject()) {
        HandleMalformedEvent(
            watch_client, event, payload,
            "event payload `data` member doesn't appear to be an object");
        return;
      }

      watch_client->OnPatch(parsed_payload["path"].GetString(),
                            parsed_payload["data"]);
    } else {
      FXL_NOTREACHED();
    }
  } else if (event == "keep-alive") {
    // Do nothing.
  } else if (event == "cancel") {
    watch_client->OnCancel();
  } else if (event == "auth_revoked") {
    watch_client->OnAuthRevoked(payload);
  } else {
    HandleMalformedEvent(watch_client, event, payload,
                         "unrecognized event type");
  }
}

void FirebaseImpl::HandleMalformedEvent(WatchClient* watch_client,
                                        const std::string& event,
                                        const std::string& payload,
                                        const char error_description[]) {
  FXL_LOG(ERROR) << "Error processing a Firebase event: " << error_description;
  FXL_LOG(ERROR) << "Event: " << event;
  FXL_LOG(ERROR) << "Data: " << payload;
  watch_client->OnMalformedEvent();
}

}  // namespace firebase
