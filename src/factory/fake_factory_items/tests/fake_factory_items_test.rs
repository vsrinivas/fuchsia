// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file_contents.

#![cfg(test)]

use anyhow::Error;
use fidl_fuchsia_boot::FactoryItemsMarker;
use fuchsia_async as fasync;
use fuchsia_component::client::connect_to_service;

#[fasync::run_singlethreaded(test)]
async fn test_get_factory_items() -> Result<(), Error> {
    let factory_items = connect_to_service::<FactoryItemsMarker>().unwrap();

    {
        let (vmo_opt, length) =
            factory_items.get(0).await.expect("Failed to get factory item with extra=0");
        let file_contents = std::fs::read("/pkg/data/empty").unwrap();
        let mut buffer = vec![0; length as usize];
        vmo_opt.unwrap().read(&mut buffer, 0).unwrap();
        assert_eq!(file_contents, buffer);
    }

    {
        let (vmo_opt, length) =
            factory_items.get(1).await.expect("Failed to get factory item with extra=1");
        let file_contents = std::fs::read("/pkg/data/random1").unwrap();
        let mut buffer = vec![0; length as usize];
        vmo_opt.unwrap().read(&mut buffer, 0).unwrap();
        assert_eq!(file_contents, buffer);
    }

    {
        let (vmo_opt, length) =
            factory_items.get(2).await.expect("Failed to get factory item with extra=2");
        let file_contents = std::fs::read("/pkg/data/random2").unwrap();
        let mut buffer = vec![0; length as usize];
        vmo_opt.unwrap().read(&mut buffer, 0).unwrap();
        assert_eq!(file_contents, buffer);
    }

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_get_factory_items_missing() -> Result<(), Error> {
    let factory_items = connect_to_service::<FactoryItemsMarker>().unwrap();

    // No item with extra=10 exists on the service.
    let (vmo_opt, length) = factory_items.get(10).await.unwrap();
    assert_eq!(true, vmo_opt.is_none());
    assert_eq!(0, length);
    Ok(())
}
