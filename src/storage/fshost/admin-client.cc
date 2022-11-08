// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.fshost/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <fidl/fuchsia.sys2/cpp/wire.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/zx/result.h>
#include <zircon/errors.h>

namespace fshost {
zx::result<fidl::ClientEnd<fuchsia_fshost::Admin>> ConnectToAdmin() {
  constexpr char kRealmQueryServicePath[] = "/svc/fuchsia.sys2.RealmQuery.root";
  constexpr char kFshostMoniker[] = "./bootstrap/fshost";

  // Connect to the root RealmQuery and get instance directories of fshost
  zx::result<fidl::ClientEnd<fuchsia_sys2::RealmQuery>> query_client_end =
      component::Connect<fuchsia_sys2::RealmQuery>(kRealmQueryServicePath);
  if (query_client_end.is_error()) {
    return query_client_end.take_error();
  }
  auto res = fidl::WireCall(query_client_end.value())->GetInstanceDirectories(kFshostMoniker);

  if (!res.ok()) {
    return zx::error(res.status());
  }
  if (res.value().is_error()) {
    return zx::error(ZX_ERR_NOT_FOUND);
  }

  // Connect to Admin protocol from exposed dir of fshost
  fidl::ClientEnd<fuchsia_io::Directory> exposed_dir =
      std::move(res->value()->resolved_dirs->exposed_dir);

  zx::result<fidl::ClientEnd<fuchsia_fshost::Admin>> client_end_res =
      component::ConnectAt<fuchsia_fshost::Admin>(exposed_dir.borrow());

  if (!client_end_res.is_ok()) {
    return zx::error(std::move(client_end_res.error_value()));
  }

  return zx::ok(std::move(client_end_res.value()));
}
}  // namespace fshost
