// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_feedback::{
        Annotation, ComponentData, ComponentDataRegisterMarker, ComponentDataRegisterProxy,
    },
    log::error,
    omaha_client::common::AppSet,
};

pub async fn publish_ids_to_feedback(app_set: AppSet) {
    let proxy =
        match fuchsia_component::client::connect_to_protocol::<ComponentDataRegisterMarker>() {
            Ok(p) => p,
            Err(e) => {
                error!("Failed to connect to component data register: {}", e);
                return;
            }
        };

    publish_ids_to_feedback_impl(proxy, app_set).await;
}

async fn publish_ids_to_feedback_impl(proxy: ComponentDataRegisterProxy, app_set: AppSet) {
    let component_data = ComponentData {
        namespace: Some("omaha".to_string()),
        annotations: Some(vec![
            Annotation { key: "app-id".to_string(), value: app_set.get_current_app_id().await },
            Annotation {
                key: "product-id".to_string(),
                value: app_set.get_current_product_id().await,
            },
        ]),
        ..ComponentData::EMPTY
    };

    match proxy.upsert(component_data).await {
        Ok(()) => {}
        Err(e) => {
            error!("Could not set ids in snapshot metadata: {}", e);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl::endpoints::create_proxy_and_stream;
    use fidl_fuchsia_feedback::ComponentDataRegisterRequest;
    use fuchsia_async as fasync;
    use futures::prelude::*;
    use omaha_client::common::App;

    #[fasync::run_singlethreaded(test)]
    async fn test_publish_ids_to_feedback() {
        let app_set = AppSet::new(vec![App::builder("some-app-id", [1, 2])
            .with_extra("product_id", "some-prod-id")
            .build()]);

        let (proxy, mut stream) = create_proxy_and_stream::<ComponentDataRegisterMarker>().unwrap();
        let stream_fut = async move {
            match stream.next().await {
                Some(Ok(ComponentDataRegisterRequest::Upsert { data, responder })) => {
                    assert_eq!(data.namespace, Some("omaha".to_string()));
                    assert_eq!(
                        data.annotations,
                        Some(vec![
                            Annotation {
                                key: "app-id".to_string(),
                                value: "some-app-id".to_string()
                            },
                            Annotation {
                                key: "product-id".to_string(),
                                value: "some-prod-id".to_string()
                            },
                        ])
                    );
                    responder.send().expect("sent response");
                }
                err => panic!("error in request handler: {:?}", err),
            }
            assert!(stream.next().await.is_none());
        };
        future::join(publish_ids_to_feedback_impl(proxy, app_set), stream_fut).await;
    }
}
