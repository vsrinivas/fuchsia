// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::builtin::capability::BuiltinCapability,
    anyhow::{anyhow, Error},
    async_trait::async_trait,
    cm_rust::CapabilityName,
    core::mem::size_of,
    fidl_fuchsia_boot as fboot,
    fuchsia_zbi::{ZbiParser, ZbiParserError, ZbiResult, ZbiType::BootloaderFile},
    fuchsia_zircon as zx,
    futures::prelude::*,
    lazy_static::lazy_static,
    routing::capability_source::InternalCapability,
    std::{collections::HashMap, convert::TryInto, str::from_utf8, sync::Arc},
};

lazy_static! {
    static ref ITEMS_CAPABILITY_NAME: CapabilityName = "fuchsia.boot.Items".into();
}

pub struct Items {
    zbi_parser: ZbiParser,
    bootloader_files: HashMap<String, Vec<u8>>,
}

impl Items {
    pub fn new(mut zbi_parser: ZbiParser) -> Result<Arc<Self>, Error> {
        // Bootloader files, if they are present in the ZBI, have special layout aware processing
        // where this service needs to parse their payload to extract the filename which is the
        // key. All other items are just stored unprocessed.
        let bootloader_files = match zbi_parser.try_get_item(BootloaderFile.into_raw(), None) {
            Ok(result) => {
                zbi_parser.release_item(BootloaderFile)?;
                Items::parse_bootloader_items(result)?
            }
            Err(_) => HashMap::new(),
        };

        Ok(Arc::new(Items { zbi_parser, bootloader_files }))
    }

    pub fn parse_bootloader_items(
        items: Vec<ZbiResult>,
    ) -> Result<HashMap<String, Vec<u8>>, Error> {
        let mut bootloader_result = HashMap::new();
        for item in items {
            // The layout of a bootloader file has the following format:
            // | size of name |    name    |     payload     |
            // 0              1       size of name    length of item
            let length = item.bytes.len();
            if length < size_of::<u8>() {
                return Err(anyhow!(
                    "Bootloader ZBI item is too small to contain the size of the name"
                ));
            }

            // The bootloader items have been split into multiple byte vectors, so this offset
            // is into an individual bootloader item starting at index 0.
            let mut offset = 0;
            let name_length = item.bytes[offset].try_into()?;
            offset += size_of::<u8>();

            if length < (offset + name_length) {
                return Err(anyhow!(
                    "Bootloader ZBI item is too small to contain the reported length of the name"
                ));
            }

            let name = from_utf8(&item.bytes[offset..(offset + name_length)])?.to_owned();
            offset = offset
                .checked_add(name_length)
                .ok_or(anyhow!("Overflow when parsing bootloader ZBI item"))?;

            if bootloader_result.contains_key(&name) {
                return Err(anyhow!("Bootloader items in ZBI have duplicate filenames: {}", name));
            }

            bootloader_result.insert(name, item.bytes[offset..].to_vec());
        }

        Ok(bootloader_result)
    }
}

#[async_trait]
impl BuiltinCapability for Items {
    const NAME: &'static str = "Items";
    type Marker = fboot::ItemsMarker;

    async fn serve(self: Arc<Self>, mut stream: fboot::ItemsRequestStream) -> Result<(), Error> {
        while let Some(request) = stream.try_next().await? {
            match request {
                fboot::ItemsRequest::Get { type_, extra, responder } => {
                    match self.zbi_parser.try_get_last_matching_item(type_, extra) {
                        Ok(result) => {
                            let vmo = zx::Vmo::create(result.bytes.len().try_into()?)?;
                            vmo.write(&result.bytes, 0)?;
                            responder.send(Some(vmo), result.bytes.len().try_into()?)?
                        }
                        Err(_) => responder.send(None, 0)?,
                    }
                }
                fboot::ItemsRequest::Get2 { type_, extra, responder } => {
                    let extra = if let Some(extra) = extra { Some((*extra).n) } else { None };
                    let mut item_vec = match self.zbi_parser.try_get_item(type_, extra) {
                        Ok(vec) => vec
                            .iter()
                            .map(|result| -> Result<fboot::RetrievedItems, Error> {
                                let vmo = zx::Vmo::create(result.bytes.len().try_into()?)?;
                                vmo.write(&result.bytes, 0)?;
                                Ok(fboot::RetrievedItems {
                                    payload: vmo,
                                    length: result.bytes.len().try_into()?,
                                    extra: result.extra,
                                })
                            })
                            .collect::<Result<Vec<fboot::RetrievedItems>, Error>>()
                            .map_err(|_| zx::Status::INTERNAL.into_raw()),
                        Err(err) => {
                            match err {
                                ZbiParserError::ItemNotStored { .. } => {
                                    Err(zx::Status::NOT_SUPPORTED.into_raw())
                                }
                                _ => {
                                    // Errors such as item not found are not unexpected, and so not
                                    // propagated to the client.
                                    Ok(vec![])
                                }
                            }
                        }
                    };

                    responder.send(&mut item_vec)?
                }
                fboot::ItemsRequest::GetBootloaderFile { filename, responder } => {
                    match self.bootloader_files.get(&filename) {
                        Some(bytes) => {
                            let vmo = zx::Vmo::create(bytes.len().try_into()?)?;
                            vmo.write(&bytes, 0)?;
                            responder.send(Some(vmo))?
                        }
                        None => responder.send(None)?,
                    }
                }
            };
        }
        Ok(())
    }

    fn matches_routed_capability(&self, capability: &InternalCapability) -> bool {
        capability.matches_protocol(&ITEMS_CAPABILITY_NAME)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        byteorder::LittleEndian,
        fuchsia_async as fasync,
        fuchsia_zbi::{
            zbi_header_t, ZbiType, ZBI_CONTAINER_MAGIC, ZBI_FLAGS_VERSION, ZBI_ITEM_MAGIC,
            ZBI_ITEM_NO_CRC32,
        },
        std::convert::TryFrom,
        zerocopy::{byteorder::U32, AsBytes},
    };

    const ZBI_HEADER_SIZE: usize = size_of::<zbi_header_t>();

    fn serve_items(zbi_parser: ZbiParser) -> Result<fboot::ItemsProxy, Error> {
        let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<fboot::ItemsMarker>()?;
        fasync::Task::local(
            Items::new(zbi_parser)?
                .serve(stream)
                .unwrap_or_else(|e| panic!("Error while serving items service: {}", e)),
        )
        .detach();
        Ok(proxy)
    }

    struct ZbiBuilder {
        zbi_bytes: Vec<u8>,
    }

    impl ZbiBuilder {
        fn get_bootloader_item(name: &[u8], payload: &[u8]) -> Vec<u8> {
            // The first byte of the bootloader item is the length of the bootloader item name
            // in bytes, and following the name is the actual payload. All three sections
            // together sum to the length reported by the header.
            let mut result = vec![name.len().try_into().unwrap()];
            result.extend(name);
            result.extend(payload);
            result
        }

        fn simple_header(zbi_type: ZbiType, extra: u32, length: u32) -> zbi_header_t {
            zbi_header_t {
                zbi_type: U32::<LittleEndian>::new(zbi_type as u32),
                length: U32::<LittleEndian>::new(length),
                extra: U32::<LittleEndian>::new(extra),
                flags: U32::<LittleEndian>::new(ZBI_FLAGS_VERSION),
                reserved_0: U32::<LittleEndian>::new(0),
                reserved_1: U32::<LittleEndian>::new(0),
                magic: U32::<LittleEndian>::new(ZBI_ITEM_MAGIC),
                crc32: U32::<LittleEndian>::new(ZBI_ITEM_NO_CRC32),
            }
        }

        fn add_item(mut self, data: &[u8]) -> Self {
            self.zbi_bytes.extend(data);
            let padding_amount = ZbiParser::align_zbi_item(data.len().try_into().unwrap()).unwrap()
                as usize
                - data.len();
            if padding_amount > 0 {
                let padding = vec![0u8; padding_amount];
                self.zbi_bytes.extend(padding);
            }
            self
        }

        fn add_header(mut self, zbi_type: ZbiType, extra: u32, length: u32) -> Self {
            self.zbi_bytes.extend(ZbiBuilder::simple_header(zbi_type, extra, length).as_bytes());
            self
        }

        fn new() -> Self {
            Self {
                zbi_bytes: ZbiBuilder::simple_header(ZbiType::Container, ZBI_CONTAINER_MAGIC, 0)
                    .as_bytes()
                    .to_vec(),
            }
        }

        fn calculate_item_length(mut self) -> Self {
            let item_length = U32::<LittleEndian>::new(
                u32::try_from(self.zbi_bytes.len() - ZBI_HEADER_SIZE).unwrap(),
            );
            let item_length_bytes = item_length.as_bytes();

            let mut i = 4usize;
            for x in item_length_bytes {
                self.zbi_bytes[i] = *x;
                i += 1;
            }

            self
        }

        fn generate(self) -> Result<zx::Vmo, Error> {
            let vmo = zx::Vmo::create(self.zbi_bytes.len().try_into()?)?;
            vmo.write(&self.zbi_bytes, 0)?;
            Ok(vmo)
        }
    }

    #[fuchsia::test]
    async fn get2_untracked_item_not_found() {
        let zbi_type = ZbiType::Crashlog;
        let item = b"abcd";

        let zbi = ZbiBuilder::new()
            .add_header(zbi_type, 0, item.len().try_into().unwrap())
            .add_item(item)
            .calculate_item_length()
            .generate()
            .expect("failed to create ZBI");
        let parser = ZbiParser::new(zbi)
            .set_store_item(ZbiType::Cmdline)
            .parse()
            .expect("failed to parse ZBI");

        let item_service = serve_items(parser).expect("failed to serve items");

        // Crashlog existed in the ZBI, but the parser configuration is set to only store Cmdline
        // items.
        let result = item_service
            .get2(zbi_type.into_raw(), None)
            .await
            .expect("failed to query item service");
        assert_eq!(zx::Status::from_raw(result.unwrap_err()), zx::Status::NOT_SUPPORTED);
    }

    #[fuchsia::test]
    async fn get2_multiple_items_same_type_different_extras() {
        let zbi_type = ZbiType::Crashlog;

        let item1 = b"abcd";
        let extra1 = 123;

        let item2 = b"efgh";
        let extra2 = 456;

        let zbi = ZbiBuilder::new()
            .add_header(zbi_type, extra1, item1.len().try_into().unwrap())
            .add_item(item1)
            .add_header(zbi_type, extra2, item2.len().try_into().unwrap())
            .add_item(item2)
            .calculate_item_length()
            .generate()
            .expect("failed to create ZBI");
        let parser = ZbiParser::new(zbi).parse().expect("failed to parse ZBI");

        let item_service = serve_items(parser).expect("failed to serve items");

        // Get both items by not specifying the extra.
        let result = item_service
            .get2(zbi_type.into_raw(), None)
            .await
            .expect("failed to query item service")
            .expect("failed to retrieve items");

        assert_eq!(result.len(), 2);

        let retrieved =
            result.iter().find(|item| item.extra == extra1).expect("failed to find item");
        let mut bytes = vec![0; retrieved.length as usize];
        retrieved.payload.read(&mut bytes, 0).expect("failed to read bytes");
        assert_eq!(bytes, item1);
        assert_eq!(retrieved.length, item1.len() as u32);

        let retrieved =
            result.iter().find(|item| item.extra == extra2).expect("failed to find item");
        let mut bytes = vec![0; retrieved.length as usize];
        retrieved.payload.read(&mut bytes, 0).expect("failed to read bytes");
        assert_eq!(bytes, item2);
        assert_eq!(retrieved.length, item2.len() as u32);

        // Get a single item by specifying the extra.
        let result = item_service
            .get2(zbi_type.into_raw(), Some(&mut fidl_fuchsia_boot::Extra { n: extra2 }))
            .await
            .expect("failed to query item service")
            .expect("failed to retrieve items");

        assert!(result.iter().find(|item| item.extra == extra2).is_some());
        assert!(result.iter().find(|item| item.extra == extra1).is_none());
    }

    #[fuchsia::test]
    async fn item_not_found_no_matching_extra() {
        // The ZBI contains the correct item, but without a matching extra.
        let actual_extra = 54321;
        let queried_extra = 12345;
        let zbi_type = ZbiType::Crashlog;

        let item = b"abcd";
        let zbi = ZbiBuilder::new()
            .add_header(zbi_type, actual_extra, item.len().try_into().unwrap())
            .add_item(item)
            .calculate_item_length()
            .generate()
            .expect("failed to create ZBI");
        let parser = ZbiParser::new(zbi).parse().expect("failed to parse ZBI");

        let item_service = serve_items(parser).expect("failed to serve items");
        let (vmo, length) = item_service
            .get(zbi_type as u32, queried_extra)
            .await
            .expect("failed to query item service");

        assert!(vmo.is_none());
        assert_eq!(length, 0);
    }

    #[fuchsia::test]
    async fn item_not_found_no_matching_zbi_type() {
        // The ZBI contains a different item with the matching extra, but no item with both
        // fields matching.
        let extra = 54321;
        let actual_zbi_type = ZbiType::Crashlog;
        let queried_zbi_type = ZbiType::KernelDriver;

        let item = b"abcd";
        let zbi = ZbiBuilder::new()
            .add_header(actual_zbi_type, extra, item.len().try_into().unwrap())
            .add_item(item)
            .calculate_item_length()
            .generate()
            .expect("failed to create ZBI");
        let parser = ZbiParser::new(zbi).parse().expect("failed to parse ZBI");

        let item_service = serve_items(parser).expect("failed to serve items");
        let (vmo, length) = item_service
            .get(queried_zbi_type as u32, extra)
            .await
            .expect("failed to query item service");

        assert!(vmo.is_none());
        assert_eq!(length, 0);
    }

    #[fuchsia::test]
    async fn get_non_bootloader_item() {
        // Both ZBI type and extra match, returning the item parsed from the ZBI.
        let zbi_type = ZbiType::Crashlog;
        let extra = 12345;

        let item = b"abcd";
        let zbi = ZbiBuilder::new()
            .add_header(zbi_type, extra, item.len().try_into().unwrap())
            .add_item(item)
            .calculate_item_length()
            .generate()
            .expect("failed to create ZBI");
        let parser = ZbiParser::new(zbi).parse().expect("failed to parse ZBI");

        let item_service = serve_items(parser).expect("failed to serve items");
        let (vmo, length) =
            item_service.get(zbi_type as u32, extra).await.expect("failed to query item service");

        assert!(vmo.is_some());
        let mut bytes = vec![0; item.len()];
        vmo.unwrap().read(&mut bytes, 0).expect("failed to read bytes");
        assert_eq!(bytes, b"abcd");
        assert_eq!(length, item.len() as u32);
    }

    #[fuchsia::test]
    async fn badly_formatted_bootloader_file() {
        // Take a correct bootloader payload, and cut it in half. The first byte reporting the
        // length of the name is now incorrect, making this an invalid entry.
        let mut item = ZbiBuilder::get_bootloader_item(b"bootloader_name", b"");
        item.resize(item.len() / 2, 0);

        let zbi = ZbiBuilder::new()
            .add_header(BootloaderFile, 0, item.len().try_into().unwrap())
            .add_item(&item)
            .calculate_item_length()
            .generate()
            .expect("failed to create ZBI");
        let parser = ZbiParser::new(zbi).parse().expect("failed to parse ZBI");

        let item_service = serve_items(parser);
        assert!(item_service.is_err());
    }

    #[fuchsia::test]
    async fn duplicate_bootloader_files() {
        let item1 = ZbiBuilder::get_bootloader_item(b"file", b"abcd");
        let item2 = ZbiBuilder::get_bootloader_item(b"file", b"efgh");

        let zbi = ZbiBuilder::new()
            .add_header(BootloaderFile, 0, item1.len().try_into().unwrap())
            .add_item(&item1)
            .add_header(BootloaderFile, 0, item2.len().try_into().unwrap())
            .add_item(&item2)
            .calculate_item_length()
            .generate()
            .expect("failed to create ZBI");
        let parser = ZbiParser::new(zbi).parse().expect("failed to parse ZBI");

        let item_service = serve_items(parser);
        assert!(item_service.is_err());
    }

    #[fuchsia::test]
    async fn no_matching_bootloader_file() {
        let actual_name = b"this_is_a_name";
        let queried_name = "this_is_NOT_a_name";
        let item = ZbiBuilder::get_bootloader_item(actual_name, b"this_is_a_payload");

        let zbi = ZbiBuilder::new()
            .add_header(BootloaderFile, 0, item.len().try_into().unwrap())
            .add_item(&item)
            .calculate_item_length()
            .generate()
            .expect("failed to create ZBI");
        let parser = ZbiParser::new(zbi).parse().expect("failed to parse ZBI");

        let item_service = serve_items(parser).expect("failed to serve items");
        let vmo = item_service
            .get_bootloader_file(queried_name)
            .await
            .expect("failed to query item service");

        assert!(vmo.is_none());
    }

    #[fuchsia::test]
    async fn get_bootloader_files() {
        let name1 = b"this_is_a_name";
        let payload1 = b"this_is_a_payload";
        let item1 = ZbiBuilder::get_bootloader_item(name1, payload1);

        let name2 = b"this_is_another_name";
        let payload2 = b"this_is_another_payload";
        let item2 = ZbiBuilder::get_bootloader_item(name2, payload2);

        let zbi = ZbiBuilder::new()
            .add_header(BootloaderFile, 0, item1.len().try_into().unwrap())
            .add_item(&item1)
            .add_header(BootloaderFile, 0, item2.len().try_into().unwrap())
            .add_item(&item2)
            .calculate_item_length()
            .generate()
            .expect("failed to create ZBI");
        let parser = ZbiParser::new(zbi).parse().expect("failed to parse ZBI");

        let item_service = serve_items(parser).expect("failed to serve items");

        let response = item_service
            .get_bootloader_file(from_utf8(name1).unwrap())
            .await
            .expect("failed to query item service");

        assert!(response.is_some());
        let vmo = response.unwrap();
        let length = vmo.get_content_size().unwrap().try_into().unwrap();
        let mut bytes = vec![0; length];
        vmo.read(&mut bytes, 0).expect("failed to read bytes");

        assert_eq!(bytes, payload1);
        assert_eq!(length, payload1.len());

        let response = item_service
            .get_bootloader_file(from_utf8(name2).unwrap())
            .await
            .expect("failed to query item service");

        assert!(response.is_some());
        let vmo = response.unwrap();
        let length = vmo.get_content_size().unwrap().try_into().unwrap();
        let mut bytes = vec![0; length];
        vmo.read(&mut bytes, 0).expect("failed to read bytes");

        assert_eq!(bytes, payload2);
        assert_eq!(length, payload2.len());
    }
}
