// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![recursion_limit = "1024"]

use {
    anyhow::{format_err, Context, Error},
    fuchsia_bluetooth::profile::{psm_from_protocol, Psm},
    fuchsia_component::server::ServiceFs,
    fuchsia_inspect as inspect,
    fuchsia_inspect_derive::Inspect,
    futures::{channel::mpsc, stream::StreamExt, FutureExt},
    profile_client::ProfileEvent,
    tracing::{error, info, warn},
};

mod browse_controller_service;
mod controller_service;
mod metrics;
mod packets;
mod peer;
mod peer_manager;
mod profile;
mod service;
mod types;

use crate::{
    metrics::{MetricsNode, METRICS_NODE_NAME},
    peer_manager::PeerManager,
    profile::{AvrcpService, AvrcpTargetFeatures},
};

async fn record_avrcp_capabilities(
    metrics_proxy: fidl_fuchsia_metrics::MetricEventLoggerProxy,
    service: &AvrcpService,
) {
    let mut event_codes_list = Vec::new();
    let remote_role = match service {
        AvrcpService::Controller { .. } => {
            bt_metrics::AvrcpRemotePeerCapabilitiesMetricDimensionRemoteRole::Controller
        }
        AvrcpService::Target { features, .. } => {
            let remote_role =
                bt_metrics::AvrcpRemotePeerCapabilitiesMetricDimensionRemoteRole::Target;
            if features.contains(AvrcpTargetFeatures::SUPPORTSCOVERART) {
                event_codes_list.push(vec![
                    remote_role as u32,
                    bt_metrics::AvrcpRemotePeerCapabilitiesMetricDimensionFeature::SupportsCoverArt
                        as u32,
                ]);
            }
            remote_role
        }
    };
    if service.supports_browsing() {
        event_codes_list.push(vec![
            remote_role as u32,
            bt_metrics::AvrcpRemotePeerCapabilitiesMetricDimensionFeature::SupportsBrowsing as u32,
        ]);
    }
    // Log the occurrences.
    for event_codes in event_codes_list {
        bt_metrics::log_on_failure(
            metrics_proxy
                .log_occurrence(
                    bt_metrics::AVRCP_REMOTE_PEER_CAPABILITIES_METRIC_ID,
                    1,
                    &event_codes,
                )
                .await,
        );
    }
}

#[fuchsia::main(logging_tags = ["avrcp"])]
async fn main() -> Result<(), Error> {
    // Begin searching for AVRCP target/controller SDP records on newly connected remote peers
    // and register our AVRCP service with the `bredr.Profile` service.
    let (profile_proxy, mut profile_client) =
        profile::connect_and_advertise().context("Unable to connect to BrEdr Profile Service")?;

    // Create a channel that peer manager will receive requests for peer controllers from the FIDL
    // service runner.
    // TODO(fxbug.dev/44330) handle back pressure correctly and reduce mpsc::channel buffer sizes.
    let (client_sender, mut service_request_receiver) = mpsc::channel(512);

    let mut fs = ServiceFs::new();

    let inspect = inspect::Inspector::new();
    inspect_runtime::serve(&inspect, &mut fs)?;

    let mut peer_manager = PeerManager::new(profile_proxy);
    if let Err(e) = peer_manager.iattach(inspect.root(), "peers") {
        warn!("Failed to attach to inspect: {:?}", e);
    }
    let mut metrics_node = MetricsNode::default();
    if let Err(e) = metrics_node.iattach(inspect.root(), METRICS_NODE_NAME) {
        warn!("Failed to attach to inspect metrics: {:?}", e);
    }
    peer_manager.set_metrics_node(metrics_node);

    // Set up cobalt 1.1 logger.
    let cobalt = bt_metrics::create_metrics_logger()
        .map_err(|e| warn!("Failed to create metrics: {e}"))
        .ok();

    let mut service_fut = service::run_services(fs, client_sender)
        .expect("Unable to start AVRCP FIDL service")
        .fuse();

    loop {
        futures::select! {
            request = profile_client.next() => {
                let request = match request {
                    None => return Err(format_err!("BR/EDR Profile unexpectedly closed")),
                    Some(Err(e)) => return Err(format_err!("Profile client error: {:?}", e)),
                    Some(Ok(r)) => r,
                };
                match request {
                    ProfileEvent::PeerConnected { id, protocol, channel } => {
                        info!("Incoming connection request from {:?} with protocol: {:?}", id, protocol);
                        let protocol = protocol.iter().map(Into::into).collect();
                        match psm_from_protocol(&protocol) {
                            Some(Psm::AVCTP) => {
                                peer_manager.new_control_connection(&id, channel);
                            },
                            Some(Psm::AVCTP_BROWSE) => {
                                peer_manager.new_browse_connection(&id, channel);
                            },
                            _ => {
                                info!("Received connection over non-AVRCP protocol: {:?}", protocol);
                            },
                        }
                    },
                    ProfileEvent::SearchResult { id, protocol, attributes } => {
                        let protocol = match protocol {
                            Some(p) => p,
                            None => {
                                info!("Received search result with no protocol, ignoring..");
                                continue;
                            }
                        };
                        match AvrcpService::from_search_result(protocol, attributes) {
                            Ok(service) => {
                                info!("Valid service found on {:?}: {:?}", id, service);
                                peer_manager.services_found(&id, vec![service]);
                                if let Some(metrics) = cobalt.clone() {
                                    record_avrcp_capabilities(metrics, &service).await;
                                }
                            }
                            Err(e) => {
                                warn!("Invalid service found: {:?}", e);
                            }
                        }
                    },
                }
            }
            request = service_request_receiver.select_next_some() => {
                peer_manager.handle_service_request(request);
            },
            service_result = service_fut => {
                error!("Service task finished unexpectedly: {:?}", service_result);
                break;
            },
            complete => break,
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use async_test_helpers::run_while;
    use async_utils::PollExt;
    use bt_metrics::respond_to_metrics_req_for_test;
    use fidl_fuchsia_metrics::*;
    use pin_utils::pin_mut;

    use crate::profile::{AvrcpControllerFeatures, AvrcpProtocolVersion};

    #[fuchsia::test]
    fn record_target_peer_capabilities() {
        let mut exec = fuchsia_async::TestExecutor::new().unwrap();
        let (proxy, mut receiver) =
            fidl::endpoints::create_proxy_and_stream::<MetricEventLoggerMarker>()
                .expect("failed to create MetricsEventLogger proxy");

        // Target with browsing and cover art features.
        let test_target = AvrcpService::Target {
            features: AvrcpTargetFeatures::from_bits_truncate(0b101000000),
            psm: Psm::AVCTP,
            protocol_version: AvrcpProtocolVersion(1, 6),
        };
        let test_fut = record_avrcp_capabilities(proxy, &test_target);

        pin_mut!(test_fut);

        let cobalt_recv_fut = receiver.next();
        let (log_request, test_fut) = run_while(&mut exec, test_fut, cobalt_recv_fut);

        // First log request for cover art.
        let got = respond_to_metrics_req_for_test(log_request.unwrap().expect("should be ok"));
        assert_eq!(bt_metrics::AVRCP_REMOTE_PEER_CAPABILITIES_METRIC_ID, got.metric_id);
        assert_eq!(vec![0, 1], got.event_codes);
        assert_eq!(MetricEventPayload::Count(1), got.payload);

        let cobalt_recv_fut = receiver.next();
        let (log_request, mut test_fut) = run_while(&mut exec, test_fut, cobalt_recv_fut);

        // Second log request for browsing.
        let got = respond_to_metrics_req_for_test(log_request.unwrap().expect("should be ok"));
        assert_eq!(bt_metrics::AVRCP_REMOTE_PEER_CAPABILITIES_METRIC_ID, got.metric_id);
        assert_eq!(vec![0, 0], got.event_codes);
        assert_eq!(MetricEventPayload::Count(1), got.payload);

        let () = exec.run_until_stalled(&mut test_fut).expect("should be done");
    }

    #[fuchsia::test]
    fn record_controller_peer_capabilities() {
        let mut exec = fuchsia_async::TestExecutor::new().unwrap();
        let (proxy, mut receiver) =
            fidl::endpoints::create_proxy_and_stream::<MetricEventLoggerMarker>()
                .expect("failed to create MetricsEventLogger proxy");

        // Target with browsing and cover art features.
        let test_controller = AvrcpService::Controller {
            features: AvrcpControllerFeatures::from_bits_truncate(0b1000000),
            psm: Psm::AVCTP,
            protocol_version: AvrcpProtocolVersion(1, 6),
        };
        let test_fut = record_avrcp_capabilities(proxy, &test_controller);

        pin_mut!(test_fut);

        let cobalt_recv_fut = receiver.next();
        let (log_request, mut test_fut) = run_while(&mut exec, test_fut, cobalt_recv_fut);

        // Log request for browsing.
        let got = respond_to_metrics_req_for_test(log_request.unwrap().expect("should be ok"));
        assert_eq!(bt_metrics::AVRCP_REMOTE_PEER_CAPABILITIES_METRIC_ID, got.metric_id);
        assert_eq!(vec![1, 0], got.event_codes);
        assert_eq!(MetricEventPayload::Count(1), got.payload);

        let () = exec.run_until_stalled(&mut test_fut).expect("should be done");
    }
}
