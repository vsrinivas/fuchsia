// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context as _, Error},
    fidl_fuchsia_hwinfo as hwinfo,
    fidl_fuchsia_intl::RegulatoryDomain,
    fidl_fuchsia_location_namedplace::{
        RegulatoryRegionConfiguratorMarker, RegulatoryRegionConfiguratorProxy,
    },
    fuchsia_component::client::connect_to_protocol,
};

/// Read region code using fuchsia.hwinfo API, then set it using the fuchsia.location.namedplace API.
/// Caller must have access to fuchsia.hwinfo.Product and fuchsia.location.namedplace.RegulatoryRegionConfigurator
/// APIs before calling this function.
pub async fn set_region_code_from_factory() -> Result<(), Error> {
    let hwinfo_proxy = connect_to_protocol::<hwinfo::ProductMarker>()
        .context("Failed to connect to hwinfo protocol")?;

    let configurator_proxy = connect_to_protocol::<RegulatoryRegionConfiguratorMarker>()
        .context("Failed to connect to Configurator protocol")?;

    let region_code = read_region_code_from_factory(&hwinfo_proxy).await?;
    set_region_code(&region_code, &configurator_proxy)
}

// Note: the hwinfo service refers to the 2-character region code as country_code, while RegulatoryRegionConfigurator
// uses the terminology RegionCode (or region_code). These refer to the same value for the intended purpose here.
async fn read_region_code_from_factory(proxy: &hwinfo::ProductProxy) -> Result<String, Error> {
    let product_info = proxy.get_info().await.context("Failed to get_info from ProductProxy")?;

    if let Some(RegulatoryDomain { country_code: Some(country_code), .. }) =
        product_info.regulatory_domain
    {
        return Ok(country_code);
    }

    Err(format_err!("No region code found, defaulting to worldwide mode (2.4GHz networks only)"))
}

fn set_region_code(
    region_code: &str,
    proxy: &RegulatoryRegionConfiguratorProxy,
) -> Result<(), Error> {
    validate_region_code(&region_code).context("Failed to validate region code")?;

    println!("Set region code: {:?}", region_code);
    proxy.set_region(&region_code).context("Set region code")?;
    Ok(())
}

fn validate_region_code(region_code: &str) -> Result<(), Error> {
    // sdk/fidl/fuchsia.location.namedplace/namedplace.fidl requires region codes to be of length 2.
    if region_code.len() != 2 {
        return Err(format_err!(
            "Invalid region code requested to set_region_code: {:?}",
            region_code
        ));
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_hwinfo as hwinfo;
    use fidl_fuchsia_intl::RegulatoryDomain;
    use fidl_fuchsia_location_namedplace as regulatory;
    use fuchsia_async as fasync;
    use fuchsia_async::TimeoutExt;
    use fuchsia_zircon::Duration;
    use futures::{channel::mpsc, StreamExt, TryStreamExt};

    fn create_mock_hwinfo_server(
        mock_info: hwinfo::ProductInfo,
    ) -> Result<hwinfo::ProductProxy, Error> {
        let (proxy, mut request_stream) =
            fidl::endpoints::create_proxy_and_stream::<hwinfo::ProductMarker>()?;

        fasync::Task::local(async move {
            while let Some(request) =
                request_stream.try_next().await.expect("failed to read mock request")
            {
                match request {
                    hwinfo::ProductRequest::GetInfo { responder } => {
                        responder.send(mock_info.clone()).ok();
                    }
                }
            }
        })
        .detach();

        Ok(proxy)
    }

    fn create_mock_regulatory_configurator_server(
    ) -> Result<(regulatory::RegulatoryRegionConfiguratorProxy, mpsc::Receiver<String>), Error>
    {
        let (mut sender, receiver) = mpsc::channel(1);
        let (proxy, mut request_stream) = fidl::endpoints::create_proxy_and_stream::<
            regulatory::RegulatoryRegionConfiguratorMarker,
        >()?;

        fasync::Task::local(async move {
            while let Some(request) =
                request_stream.try_next().await.expect("failed to read mock request")
            {
                match request {
                    regulatory::RegulatoryRegionConfiguratorRequest::SetRegion {
                        region,
                        control_handle: _,
                    } => {
                        sender.start_send(region).unwrap();
                    }
                }
            }
        })
        .detach();

        Ok((proxy, receiver))
    }

    #[fuchsia::test]
    async fn test_read_from_hwinfo_success() {
        // We need both the regulatory_domain and country_code fields to be populated for success.
        let expected_region_code = "AA".to_string();
        let mut regulatory_domain = RegulatoryDomain::EMPTY;
        regulatory_domain.country_code = Some(expected_region_code.clone());

        let mut product_info = hwinfo::ProductInfo::EMPTY;
        product_info.regulatory_domain = Some(regulatory_domain);

        let proxy = create_mock_hwinfo_server(product_info).unwrap();

        let region_code = read_region_code_from_factory(&proxy).await.unwrap();

        assert_eq!(region_code, expected_region_code);
    }

    #[fuchsia::test]
    async fn test_read_from_hwinfo_no_regulatory_domain_returns_error() {
        let product_info = hwinfo::ProductInfo::EMPTY;
        let proxy = create_mock_hwinfo_server(product_info).unwrap();

        let result = read_region_code_from_factory(&proxy).await;

        assert!(result.is_err());
        assert_eq!(
            format!("{}", result.unwrap_err()),
            "No region code found, defaulting to worldwide mode (2.4GHz networks only)"
        );
    }

    #[fuchsia::test]
    async fn test_read_from_hwinfo_no_country_code_returns_error() {
        let regulatory_domain = RegulatoryDomain::EMPTY;

        let mut product_info = hwinfo::ProductInfo::EMPTY;
        product_info.regulatory_domain = Some(regulatory_domain);
        let proxy = create_mock_hwinfo_server(product_info).unwrap();

        let result = read_region_code_from_factory(&proxy).await;

        assert!(result.is_err());
        assert_eq!(
            format!("{}", result.unwrap_err()),
            "No region code found, defaulting to worldwide mode (2.4GHz networks only)"
        );
    }

    #[fuchsia::test]
    async fn test_set_region_code_success() {
        let valid_region_code = "AA".to_string();

        let (proxy, mut receiver) = create_mock_regulatory_configurator_server().unwrap();

        set_region_code(&valid_region_code, &proxy).unwrap();

        let region_code_received =
            receiver.next().on_timeout(Duration::from_seconds(5), || None).await.unwrap();
        assert_eq!(region_code_received, valid_region_code);
    }

    #[fuchsia::test]
    async fn test_set_invalid_region_code_returns_error() {
        // This will fail the validation. The proxy shouldn't see any calls coming through with the invalid code.
        let invalid_region_code = "a".to_string();

        let (proxy, mut receiver) = create_mock_regulatory_configurator_server().unwrap();

        let result = set_region_code(&invalid_region_code, &proxy);

        assert!(result.is_err());
        // try_next will return error if there are no messages waiting, and the channel is closed.
        assert!(receiver.try_next().is_err());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_valid_region_codes() {
        let valid_codes = vec!["AA", "ZZ"];

        for code in valid_codes {
            validate_region_code(code).unwrap();
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_invalid_region_codes() {
        let invalid_codes = vec!["", "a", "test"];

        for code in invalid_codes {
            validate_region_code(code).unwrap_err();
        }
    }
}
