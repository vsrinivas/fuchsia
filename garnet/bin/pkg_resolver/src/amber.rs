// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_amber::{ControlProxy as AmberProxy, Status as AmberStatus},
    fuchsia_syslog::{fx_log_err, fx_log_info},
    futures::{future::BoxFuture, prelude::*, stream::FuturesUnordered},
};

/// High level actions performed on amber by the package resolver to manipulate source configs.
pub trait AmberSourceSelector: Sized + Clone + Send {
    /// Disable all enabled sources.
    fn disable_all_sources(&self) -> BoxFuture<'_, ()>;

    /// Enable the given source id, disabling all other enabled sources. If no source with the
    /// given `id` exists, disable all enabled sources.
    fn enable_source_exclusive(&self, id: &str) -> BoxFuture<'_, ()>;
}

impl AmberSourceSelector for AmberProxy {
    fn disable_all_sources(&self) -> BoxFuture<'_, ()> {
        let amber = self.clone();
        async move {
            let mut work = FuturesUnordered::new();
            for (source_id, enabled) in await!(iter_sources(&amber)) {
                if enabled {
                    work.push(set_source_enabled(&amber, source_id, false));
                }
            }
            await!(work.collect::<()>());
        }
            .boxed()
    }
    fn enable_source_exclusive(&self, id: &str) -> BoxFuture<'_, ()> {
        let amber = self.clone();
        let id = id.to_owned();

        async move {
            let mut work = FuturesUnordered::new();
            // Rewrite rules don't require hostnames to be registered source or repository configs.
            // If id doesn't reference a valid source config, try to enable it anyway for the
            // syslog output. Note that this case would result in all sources being disabled.
            work.push(set_source_enabled(&amber, id.clone(), true));
            for (source_id, enabled) in await!(iter_sources(&amber)) {
                if enabled && source_id != id {
                    work.push(set_source_enabled(&amber, source_id, false));
                }
            }
            await!(work.collect::<()>());
        }
            .boxed()
    }
}

async fn iter_sources(amber: &AmberProxy) -> impl Iterator<Item = (String, bool)> {
    await!(amber.list_srcs())
        .unwrap_or_else(|e| {
            fx_log_err!("while listing Amber sources: {:?}", e);
            vec![]
        })
        .into_iter()
        .map(|source| {
            let enabled = source.status_config.map(|status| status.enabled).unwrap_or(true);
            (source.id, enabled)
        })
}

async fn set_source_enabled(amber: &AmberProxy, id: String, enabled: bool) {
    let verb = if enabled { "enabl" } else { "disabl" };
    fx_log_info!("{}ing source {}", verb, id.as_str());
    match await!(amber.set_src_enabled(id.as_str(), enabled)) {
        Ok(AmberStatus::Ok) => {}
        Ok(status) => {
            fx_log_err!("unable to {}e Amber source {:?}: {:?}", verb, id, status);
        }
        Err(err) => {
            fx_log_err!("while {}ing Amber source {:?}: {:?}", verb, id, err);
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        failure::Error,
        fidl_fuchsia_amber::{
            ControlMarker as AmberMarker, ControlRequest as Request,
            ControlRequestStream as RequestStream, SourceConfig, Status,
        },
        fidl_fuchsia_amber_ext::SourceConfigBuilder,
        fuchsia_async as fasync,
    };

    const ROOT_KEY: &str = "00112233445566778899aabbccddeeffffeeddccbbaa99887766554433221100";

    enum Enablement {
        Enabled,
        Disabled,
    }
    use Enablement::*;

    fn make_source(id: &str, enabled: Enablement) -> SourceConfig {
        SourceConfigBuilder::new(id)
            .add_root_key(ROOT_KEY)
            .enabled(match enabled {
                Enabled => true,
                Disabled => false,
            })
            .build()
            .into()
    }

    #[derive(Debug, Clone, PartialEq, Eq, PartialOrd, Ord)]
    enum Record {
        ListSrcs,
        SetSrcEnabled { id: String, enabled: bool },
    }

    async fn serve_amber(stream: RequestStream, mut sources: Vec<SourceConfig>) -> Vec<Record> {
        await!(stream
            .map(|res| res.unwrap())
            .map(|request| {
                match request {
                    Request::ListSrcs { responder } => {
                        responder.send(&mut sources.iter_mut()).unwrap();
                        Record::ListSrcs
                    }
                    Request::SetSrcEnabled { id, enabled, responder } => {
                        responder.send(Status::Ok).unwrap();
                        Record::SetSrcEnabled { id, enabled }
                    }
                    _ => panic!("unexpected request"),
                }
            })
            .collect::<Vec<_>>())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_disable_all_sources() -> Result<(), Error> {
        let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<AmberMarker>()?;
        fasync::spawn(async move {
            await!(proxy.disable_all_sources());
        });

        let records = await!(serve_amber(
            stream,
            vec![make_source("disabled", Disabled), make_source("enabled", Enabled)]
        ));

        assert_eq!(
            records,
            vec![
                Record::ListSrcs,
                Record::SetSrcEnabled { id: "enabled".to_owned(), enabled: false }
            ]
        );

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_enable_source_exclusive() -> Result<(), Error> {
        let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<AmberMarker>()?;
        fasync::spawn(async move {
            await!(proxy.enable_source_exclusive("to_enable"));
        });

        let sources = vec![
            make_source("disabled", Disabled),
            make_source("to_disable1", Enabled),
            make_source("to_disable2", Enabled),
            make_source("to_enable", Disabled),
        ];
        let mut records = await!(serve_amber(stream, sources));

        // List must happen first.
        assert_eq!(records.remove(0), Record::ListSrcs);

        // The source management requests can be handled in any order.
        records.sort_unstable();
        assert_eq!(
            records,
            vec![
                Record::SetSrcEnabled { id: "to_disable1".to_owned(), enabled: false },
                Record::SetSrcEnabled { id: "to_disable2".to_owned(), enabled: false },
                Record::SetSrcEnabled { id: "to_enable".to_owned(), enabled: true },
            ]
        );

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_attempts_to_enable_nonexistant_source() -> Result<(), Error> {
        let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<AmberMarker>()?;
        fasync::spawn(async move {
            await!(proxy.enable_source_exclusive("does_not_exist"));
        });

        let sources =
            vec![make_source("to_disable", Enabled), make_source("already_disabled", Disabled)];
        let mut records = await!(serve_amber(stream, sources));

        // List must happen first.
        assert_eq!(records.remove(0), Record::ListSrcs);

        // The source management requests can be handled in any order.
        records.sort_unstable();
        assert_eq!(
            records,
            vec![
                Record::SetSrcEnabled { id: "does_not_exist".to_owned(), enabled: true },
                Record::SetSrcEnabled { id: "to_disable".to_owned(), enabled: false },
            ]
        );

        Ok(())
    }
}
