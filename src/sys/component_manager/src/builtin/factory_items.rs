// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::builtin::capability::BuiltinCapability,
    anyhow::{anyhow, Error},
    async_trait::async_trait,
    cm_rust::CapabilityName,
    fidl_fuchsia_boot as fboot,
    fuchsia_zbi::{ZbiParser, ZbiParserError, ZbiResult, ZbiType::StorageBootfsFactory},
    fuchsia_zircon::{self as zx, HandleBased},
    futures::prelude::*,
    lazy_static::lazy_static,
    routing::capability_source::InternalCapability,
    std::{collections::HashMap, convert::TryInto, sync::Arc},
};

lazy_static! {
    static ref FACTORY_ITEMS_CAPABILITY_NAME: CapabilityName = "fuchsia.boot.FactoryItems".into();

    // The default rights for an immutable VMO. For details see
    // https://fuchsia.dev/fuchsia-src/reference/syscalls/vmo_create#description.
    static ref IMMUTABLE_VMO_RIGHTS: zx::Rights = zx::Rights::DUPLICATE
        | zx::Rights::TRANSFER
        | zx::Rights::READ
        | zx::Rights::MAP
        | zx::Rights::GET_PROPERTY;
}

#[derive(Debug)]
struct FactoryItem {
    vmo: zx::Vmo,
    length: u32,
}

#[derive(Debug)]
pub struct FactoryItems {
    // The key of this HashMap is the "extra" field of the zbi_header_t.
    items: HashMap<u32, FactoryItem>,
}

impl FactoryItems {
    pub fn new(parser: &mut ZbiParser) -> Result<Arc<Self>, Error> {
        match parser.try_get_item(StorageBootfsFactory) {
            Ok(result) => {
                parser.release_item(StorageBootfsFactory)?;
                FactoryItems::from_parsed_zbi(result)
            }
            Err(err) if err == ZbiParserError::ItemNotFound { zbi_type: StorageBootfsFactory } => {
                // It's not an unexpected error to not have any StorageBootfsFactory items in the
                // ZBI. This service will just return None to any queries.
                Ok(Arc::new(FactoryItems { items: HashMap::new() }))
            }
            Err(err) => {
                // Any error besides ItemNotFound is unexpected, and fatal.
                Err(anyhow!(
                    "Failed to retrieve StorageBootfsFactory item with unexpected error: {}",
                    err
                ))
            }
        }
    }

    fn from_parsed_zbi(items: Vec<ZbiResult>) -> Result<Arc<Self>, Error> {
        let mut parsed_items = HashMap::new();
        for item in items {
            // The factory items service uses the ZBI extra field as a lookup key. There can
            // be many factory items in the ZBI, but each extra field must be unique.
            if parsed_items.contains_key(&item.extra) {
                return Err(anyhow!("Duplicate factory item found in ZBI: {}", item.extra));
            }

            let vmo = zx::Vmo::create(item.bytes.len().try_into()?)?;
            vmo.write(&item.bytes, 0)?;
            parsed_items
                .insert(item.extra, FactoryItem { vmo, length: item.bytes.len().try_into()? });
        }

        Ok(Arc::new(FactoryItems { items: parsed_items }))
    }
}

#[async_trait]
impl BuiltinCapability for FactoryItems {
    const NAME: &'static str = "FactoryItems";
    type Marker = fboot::FactoryItemsMarker;

    async fn serve(
        self: Arc<Self>,
        mut stream: fboot::FactoryItemsRequestStream,
    ) -> Result<(), Error> {
        while let Some(fboot::FactoryItemsRequest::Get { extra, responder }) =
            stream.try_next().await?
        {
            match self.items.get(&extra) {
                Some(item) => responder
                    .send(Some(item.vmo.duplicate_handle(*IMMUTABLE_VMO_RIGHTS)?), item.length)?,
                None => responder.send(None, 0)?,
            };
        }
        Ok(())
    }

    fn matches_routed_capability(&self, capability: &InternalCapability) -> bool {
        capability.matches_protocol(&FACTORY_ITEMS_CAPABILITY_NAME)
    }
}

#[cfg(test)]
mod tests {
    use {super::*, fidl::AsHandleRef, fuchsia_async as fasync};

    fn serve_factory_items(items: Vec<ZbiResult>) -> Result<fboot::FactoryItemsProxy, Error> {
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fboot::FactoryItemsMarker>()?;
        fasync::Task::local(
            FactoryItems::from_parsed_zbi(items)?
                .serve(stream)
                .unwrap_or_else(|e| panic!("Error while serving factory items service: {}", e)),
        )
        .detach();
        Ok(proxy)
    }

    #[fuchsia::test]
    async fn no_factory_items_in_zbi() {
        let mut parser = ZbiParser::new(zx::Vmo::create(0).expect("Failed to create empty VMO"));
        let items = FactoryItems::new(&mut parser);

        // It's not an error for there to be no factory items in the ZBI.
        assert!(items.is_ok());
        assert_eq!(items.unwrap().items.len(), 0);
    }

    #[fuchsia::test]
    async fn duplicate_factory_item() {
        // Note that two ZbiResults have the same 'extra' value, and this value is what's used
        // for ths service's key.
        let mock_results = vec![
            ZbiResult { bytes: b"abc".to_vec(), extra: 12345 },
            ZbiResult { bytes: b"def".to_vec(), extra: 12345 },
            ZbiResult { bytes: b"ghi".to_vec(), extra: 54321 },
        ];

        let factory_items = serve_factory_items(mock_results);
        assert!(factory_items.is_err());
    }

    #[fuchsia::test]
    async fn no_matching_factory_item() {
        let mock_results = vec![
            ZbiResult { bytes: b"abc".to_vec(), extra: 123 },
            ZbiResult { bytes: b"def".to_vec(), extra: 456 },
            ZbiResult { bytes: b"ghi".to_vec(), extra: 789 },
        ];

        let factory_items = serve_factory_items(mock_results).unwrap();
        let (vmo, length) =
            factory_items.get(314159).await.expect("Failed to query factory item service");

        assert!(vmo.is_none());
        assert_eq!(length, 0);
    }

    #[fuchsia::test]
    async fn get_factory_items_success() {
        let mock_results = vec![
            ZbiResult { bytes: b"abc".to_vec(), extra: 123 },
            ZbiResult { bytes: b"def".to_vec(), extra: 456 },
            ZbiResult { bytes: b"ghi".to_vec(), extra: 789 },
        ];

        let factory_items = serve_factory_items(mock_results).unwrap();
        let (vmo, length) =
            factory_items.get(456).await.expect("Failed to query factory item service");

        let mut bytes = [0; b"def".len()];
        assert_eq!(length, bytes.len() as u32);

        assert!(vmo.is_some());
        let vmo = vmo.unwrap();
        vmo.read(&mut bytes, 0).unwrap();
        assert_eq!(&bytes, b"def");

        let rights = vmo.basic_info().unwrap().rights;

        // VMO has default immutable rights.
        assert!(rights.contains(zx::Rights::DUPLICATE));
        assert!(rights.contains(zx::Rights::TRANSFER));
        assert!(rights.contains(zx::Rights::READ));
        assert!(rights.contains(zx::Rights::MAP));
        assert!(rights.contains(zx::Rights::GET_PROPERTY));

        // VMO does not have any mutable rights.
        assert!(!rights.contains(zx::Rights::WRITE));
        assert!(!rights.contains(zx::Rights::SET_PROPERTY));
    }
}
