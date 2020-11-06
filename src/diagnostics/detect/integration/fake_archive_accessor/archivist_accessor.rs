// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This code is adapted and simplified from
// fuchsia-mirror/src/diagnostics/archivist/src/accessor.rs
// Unlike the original, it does not spawn tasks; it's fully synchronous.

use {
    super::archivist_server::{AccessorServer, ServerError},
    anyhow::{bail, format_err, Error},
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_diagnostics as diagnostics,
    fidl_fuchsia_diagnostics::{
        self, ClientSelectorConfiguration, DataType, Format, Selector, SelectorArgument, StreamMode,
    },
    log::warn,
    selectors,
};

/// ArchiveAccessor serves an incoming connection from a client to an Archivist
/// instance, through which the client may make Reader requests to get Inspect
/// data for the test.
pub struct ArchiveAccessor {}

fn validate_and_parse_inspect_selectors(
    selector_args: Vec<SelectorArgument>,
) -> Result<Vec<Selector>, ServerError> {
    let mut selectors = vec![];
    if selector_args.is_empty() {
        Err(ServerError::EmptySelectors)?;
    }

    for selector_arg in selector_args {
        let selector = match selector_arg {
            SelectorArgument::StructuredSelector(s) => selectors::validate_selector(&s).map(|_| s),
            SelectorArgument::RawSelector(r) => selectors::parse_selector(&r),
            _ => Err(format_err!("unrecognized selector configuration")),
        }
        .map_err(ServerError::ParseSelectors)?;

        selectors.push(selector);
    }

    Ok(selectors)
}

impl ArchiveAccessor {
    pub fn validate_stream_request(
        params: fidl_fuchsia_diagnostics::StreamParameters,
    ) -> Result<(), ServerError> {
        let format = params.format.ok_or(ServerError::MissingFormat)?;
        if !matches!(format, Format::Json) {
            return Err(ServerError::UnsupportedFormat);
        }
        let mode = params.stream_mode.ok_or(ServerError::MissingMode)?;
        if !matches!(mode, StreamMode::Snapshot) {
            return Err(ServerError::UnsupportedMode);
        }
        let data_type = params.data_type.ok_or(ServerError::MissingDataType)?;
        if !matches!(data_type, DataType::Inspect) {
            return Err(ServerError::UnsupportedType);
        }
        let selectors =
            params.client_selector_configuration.ok_or(ServerError::MissingSelectors)?;
        match selectors {
            ClientSelectorConfiguration::Selectors(selectors) => {
                validate_and_parse_inspect_selectors(selectors)?;
            }
            ClientSelectorConfiguration::SelectAll(_) => {
                return Err(ServerError::InvalidSelectors("Don't ask for all Inspect data"));
            }
            _ => Err(ServerError::InvalidSelectors("unrecognized selectors"))?,
        };
        Ok(())
    }

    /// Run synchronously an instance `fidl_fuchsia_diagnostics/Archive` that allows clients to open
    /// reader session to diagnostics data.
    pub async fn send(
        result_stream: ServerEnd<diagnostics::BatchIteratorMarker>,
        inspect_data: &String,
    ) -> Result<(), Error> {
        let (requests, control) = match result_stream.into_stream_and_control_handle() {
            Ok(r) => r,
            Err(e) => {
                warn!("Couldn't bind results channel to executor: {:?}", e);
                bail!("Couldn't bind results channel to executor: {:?}", e);
            }
        };

        if let Err(e) = AccessorServer::new(requests).send(inspect_data).await {
            e.close(control);
        }
        Ok(())
    }
}
