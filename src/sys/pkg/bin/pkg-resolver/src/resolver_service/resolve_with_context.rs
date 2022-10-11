// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::QueuedResolver, crate::eager_package_manager::EagerPackageManager,
    fidl_fuchsia_io as fio, fidl_fuchsia_metrics as fmetrics, fidl_fuchsia_pkg as fpkg,
    fidl_fuchsia_pkg_ext as pkg, fuchsia_syslog::fx_log_err,
};

pub(super) async fn resolve_with_context(
    package_url: String,
    context: fpkg::ResolutionContext,
    dir: fidl::endpoints::ServerEnd<fio::DirectoryMarker>,
    package_resolver: &QueuedResolver,
    eager_package_manager: Option<&async_lock::RwLock<EagerPackageManager<QueuedResolver>>>,
    cobalt_sender: fidl_contrib::protocol_connector::ProtocolSender<fmetrics::MetricEvent>,
) -> Result<fpkg::ResolutionContext, pkg::ResolveError> {
    match fuchsia_url::PackageUrl::parse(&package_url)
        .map_err(|e| super::handle_bad_package_url_error(e, &package_url))?
    {
        fuchsia_url::PackageUrl::Absolute(url) => {
            if !context.bytes.is_empty() {
                fx_log_err!(
                    "ResolveWithContext context must be empty if url is absolute {} {:?}",
                    package_url,
                    context,
                );
                return Err(pkg::ResolveError::InvalidContext);
            }
            return super::resolve_absolute_url_and_send_cobalt_metrics(
                url,
                dir,
                package_resolver,
                eager_package_manager,
                cobalt_sender,
            )
            .await;
        }
        fuchsia_url::PackageUrl::Relative(_) => {
            fx_log_err!(
                "ResolveWithContext for relative urls is not currently implemented. \
                 Could not resolve {:?} with context {:?}",
                package_url,
                context,
            );
            return Err(pkg::ResolveError::Internal);
        }
    }
}
