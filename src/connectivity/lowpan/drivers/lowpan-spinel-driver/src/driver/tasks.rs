// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;
use crate::prelude::*;
use crate::spinel::*;

use crate::spinel::Subnet;
use anyhow::{Context as _, Error};
use futures::prelude::*;
use lowpan_driver_common::FutureExt;

impl<DS: SpinelDeviceClient, NI: NetworkInterface> SpinelDriver<DS, NI> {
    fn outbound_packet_pump(&self) -> impl TryStream<Ok = (), Error = Error> + Send + '_ {
        futures::stream::try_unfold((), move |()| async move {
            // Get the outbound network packet from netstack
            let packet = self.net_if.outbound_packet_from_stack().await?;

            fx_log_info!("Outbound packet from netstack: {}", hex::encode(&packet));

            // Send the outbound network packet to the NCP.
            let _ = self
                .frame_handler
                .send_request_ignore_response(CmdPropValueSet(
                    PropStream::Net.into(),
                    NetworkPacket { packet: &packet, metadata: &[] },
                ))
                .await;

            // Continue processing.
            Ok(Some(((), ())))
        })
    }

    async fn handle_netstack_added_address(&self, subnet: Subnet) -> Result<(), Error> {
        fx_log_info!("Netstack added address: {:?} (ignored)", subnet);

        let addr_entry = AddressTableEntry { subnet };

        // Wait for our turn.
        let _lock = self.wait_for_api_task_lock("handle_netstack_added_address").await?;

        let is_existing_address = {
            let driver_state = self.driver_state.lock();
            driver_state.address_table.contains(&addr_entry)
        };

        if !is_existing_address {
            self.frame_handler
                .send_request(CmdPropValueInsert(PropIpv6::AddressTable.into(), addr_entry.clone()))
                .or_else(move |err| async move {
                    fx_log_warn!(
                        "NCP refused to insert {:?} into PropIpv6::AddressTable, will remove. {:?}",
                        &addr_entry,
                        err
                    );
                    self.net_if.remove_address(&addr_entry.subnet)
                })
                .await
                .context("handle_netstack_added_address")?;
        }

        Ok(())
    }

    async fn handle_netstack_removed_address(&self, subnet: Subnet) -> Result<(), Error> {
        fx_log_info!("Netstack removed address: {:?} (ignored)", subnet);

        let addr_entry = AddressTableEntry { subnet };

        // Wait for our turn.
        let _lock = self.wait_for_api_task_lock("handle_netstack_removed_address").await?;

        let is_existing_address = {
            let driver_state = self.driver_state.lock();
            driver_state.address_table.contains(&addr_entry)
        };

        if is_existing_address {
            self.frame_handler
                .send_request(CmdPropValueRemove(PropIpv6::AddressTable.into(), addr_entry))
                .await
                .context("handle_netstack_removed_address")?;
        }

        Ok(())
    }

    async fn handle_netstack_added_route(&self, subnet: Subnet) -> Result<(), Error> {
        fx_log_info!("Netstack added route: {:?} (ignored)", subnet);
        // TODO: Writeme!
        Ok(())
    }

    async fn handle_netstack_removed_route(&self, subnet: Subnet) -> Result<(), Error> {
        fx_log_info!("Netstack removed route: {:?} (ignored)", subnet);
        // TODO: Writeme!
        Ok(())
    }
}

/// Background Tasks
///
/// These are tasks which are ultimately called from
/// `main_loop()`. They are intended to run in parallel
/// with API-related tasks.
impl<DS: SpinelDeviceClient, NI: NetworkInterface> SpinelDriver<DS, NI> {
    /// A single iteration of the main loop
    async fn single_main_loop(&self) -> Result<(), Error> {
        let (init_state, connectivity_state) = {
            let x = self.driver_state.lock();
            (x.init_state, x.connectivity_state)
        };
        Ok(match init_state {
            InitState::Initialized if connectivity_state.is_active_and_ready() => {
                fx_log_info!("main_task: Initialized, active, and ready");

                let exit_criteria = self.wait_for_state(|x| {
                    x.is_initializing() || !x.connectivity_state.is_active_and_ready()
                });

                self.online_task()
                    .boxed()
                    .map(|x| match x {
                        Err(err) if err.is::<Canceled>() => Ok(()),
                        other => other,
                    })
                    .cancel_upon(exit_criteria.boxed(), Ok(()))
                    .map_err(|x| x.context("online_task"))
                    .await?;

                fx_log_info!("main_task: online_task terminated");

                self.online_task_cleanup()
                    .boxed()
                    .map(|x| match x {
                        Err(err) if err.is::<Canceled>() => Ok(()),
                        other => other,
                    })
                    .map_err(|x| x.context("online_task_cleanup"))
                    .await?;
            }

            InitState::Initialized => {
                fx_log_info!("main_task: Initialized, but either not active or not ready.");

                let exit_criteria = self.wait_for_state(|x| {
                    x.is_initializing() || x.connectivity_state.is_active_and_ready()
                });

                self.offline_task()
                    .boxed()
                    .map(|x| match x {
                        Err(err) if err.is::<Canceled>() => Ok(()),
                        other => other,
                    })
                    .cancel_upon(exit_criteria.boxed(), Ok(()))
                    .map_err(|x| x.context("offline_task"))
                    .await?;

                fx_log_info!("main_task: offline_task terminated");
            }

            _ => {
                fx_log_info!("main_task: Uninitialized, starting initialization task");

                // We are not initialized, start the init task.
                self.init_task().map_err(|x| x.context("init_task")).await?;
            }
        })
    }

    /// Main loop task that handles the high-level tasks for the driver.
    ///
    /// This task is intended to run continuously and will not normally
    /// terminate. However, it will terminate upon I/O errors and frame
    /// unpacking errors.
    ///
    /// This method must only be invoked once. Invoking it more than once
    /// will cause a panic.
    ///
    /// This method is called from `wrap_inbound_stream()` in `inbound.rs`.
    pub(super) async fn take_main_task(&self) -> Result<(), Error> {
        if self.did_vend_main_task.swap(true, std::sync::atomic::Ordering::Relaxed) {
            panic!("take_main_task must only be called once");
        }

        let net_if_event_stream = self.net_if.take_event_stream().and_then(|x| async move {
            Ok(match x {
                NetworkInterfaceEvent::InterfaceEnabledChanged(enabled) => {
                    let mut driver_state = self.driver_state.lock();

                    let new_connectivity_state = if enabled {
                        driver_state.connectivity_state.activated()
                    } else {
                        driver_state.connectivity_state.deactivated()
                    };

                    if new_connectivity_state != driver_state.connectivity_state {
                        let old_connectivity_state = driver_state.connectivity_state;
                        driver_state.connectivity_state = new_connectivity_state;
                        std::mem::drop(driver_state);
                        self.driver_state_change.trigger();
                        self.on_connectivity_state_change(
                            new_connectivity_state,
                            old_connectivity_state,
                        );
                    }
                }
                NetworkInterfaceEvent::AddressWasAdded(x) => {
                    self.handle_netstack_added_address(x).await?
                }
                NetworkInterfaceEvent::AddressWasRemoved(x) => {
                    self.handle_netstack_removed_address(x).await?
                }
                NetworkInterfaceEvent::RouteToSubnetProvided(x) => {
                    self.handle_netstack_added_route(x).await?
                }
                NetworkInterfaceEvent::RouteToSubnetRevoked(x) => {
                    self.handle_netstack_removed_route(x).await?
                }
            })
        });

        let main_loop_stream = futures::stream::try_unfold((), move |_| {
            self.single_main_loop()
                .map_ok(|x| Some((x, ())))
                .map_err(|x| x.context("single_main_loop"))
        });

        futures::stream::select(main_loop_stream.into_stream(), net_if_event_stream)
            .try_collect::<()>()
            .await
    }

    async fn sync_addresses(&self) -> Result<(), Error> {
        let driver_state = self.driver_state.lock();

        // Add the link-local address first, so that it is at the top of the list.
        if let Err(err) = self.net_if.add_address(&Subnet {
            addr: driver_state.link_local_addr.clone(),
            prefix_len: STD_IPV6_NET_PREFIX_LEN,
        }) {
            fx_log_err!("Unable to add address `{}`: {:?}", driver_state.link_local_addr, err);
        }

        // Add the mesh-local address second, so that it is the next address in the list.
        if let Err(err) = self.net_if.add_address(&Subnet {
            addr: driver_state.mesh_local_addr.clone(),
            prefix_len: STD_IPV6_NET_PREFIX_LEN,
        }) {
            fx_log_err!("Unable to add address `{}`: {:?}", driver_state.mesh_local_addr, err);
        }

        // Add the rest of the addresses
        for entry in driver_state.address_table.iter() {
            // Skip re-adding the link local address.
            if entry.subnet.addr == driver_state.link_local_addr {
                continue;
            }

            // Skip any addresses that have a mesh-local prefix.
            if driver_state.addr_is_mesh_local(&entry.subnet.addr) {
                continue;
            }

            if let Err(err) = self.net_if.add_address(&entry.subnet) {
                fx_log_err!("Unable to add address `{}`: {:?}", entry.subnet.addr, err);
            }
        }

        Ok(())
    }

    /// Online loop task that is executed while we are both "ready" and "active".
    ///
    /// This task will bring the device into a state where it
    /// is an active participant in the network.
    ///
    /// The resulting future may be terminated at any time.
    async fn online_task(&self) -> Result<(), Error> {
        fx_log_info!("online_loop: Entered");

        {
            // Wait for our turn.
            let _lock = self.wait_for_api_task_lock("online_task").await?;

            // Bring up the network interface.
            self.frame_handler
                .send_request(CmdPropValueSet(PropNet::InterfaceUp.into(), true).verify())
                .await
                .context("Setting PropNet::InterfaceUp")?;

            // Bring up the mesh stack.
            self.frame_handler
                .send_request(CmdPropValueSet(PropNet::StackUp.into(), true).verify())
                .await
                .context("Setting PropNet::StackUp")?;
        }

        fx_log_info!("online_loop: Waiting for us to become online. . .");

        self.wait_for_state(|x| x.connectivity_state != ConnectivityState::Attaching).await;

        let connectivity_state = self.get_connectivity_state();

        if connectivity_state.is_online() {
            // Mark the network interface as online.
            self.net_if.set_online(true).await.context("Marking network interface as online")?;

            self.sync_addresses().await?;
        } else {
            Err(format_err!("Unexpected connectivity state: {:?}", connectivity_state))?
        }

        fx_log_info!("online_loop: We are online, starting outbound packet pump");

        // Run the pump that pulls outbound data from netstack to the NCP.
        // This will run indefinitely.
        self.outbound_packet_pump()
            .into_stream()
            .try_collect::<()>()
            .await
            .context("outbound_packet_pump")
    }

    /// Cleanup method that is called after the online task has finished.
    async fn online_task_cleanup(&self) -> Result<(), Error> {
        self.net_if
            .set_online(false)
            .await
            .context("Unable to mark network interface as offline")?;

        let driver_state = self.driver_state.lock();
        for entry in driver_state.address_table.iter() {
            if let Err(err) = self.net_if.remove_address(&entry.subnet) {
                fx_log_err!("Unable to remove address: {:?}", err);
            }
        }

        Ok(())
    }

    /// Offline loop task that is executed while we are either "not ready" or "inactive".
    ///
    /// This task will bring the device to a state where
    /// it is not an active participant in the network.
    ///
    /// The resulting future may be terminated at any time.
    async fn offline_task(&self) -> Result<(), Error> {
        fx_log_info!("offline_loop: Entered");

        {
            // Scope for the API task lock.
            // Wait for our turn.
            let _lock = self.wait_for_api_task_lock("offline_task").await?;

            // Mark the network interface as offline.
            self.net_if
                .set_online(false)
                .await
                .context("Unable to mark network interface as offline")?;

            // Bring down the mesh stack.
            if let Err(err) = self
                .frame_handler
                .send_request(CmdPropValueSet(PropNet::StackUp.into(), false))
                .await
                .context("Setting PropNet::StackUp to False")
            {
                fx_log_err!("Unable to set `PropNet::StackUp`: {:?}", err);
            }

            // Bring down the network interface.
            if let Err(err) = self
                .frame_handler
                .send_request(CmdPropValueSet(PropNet::InterfaceUp.into(), false))
                .await
                .context("Setting PropNet::InterfaceUp to False")
            {
                fx_log_err!("Unable to set `PropNet::InterfaceUp`: {:?}", err);
            }
        } // API task lock goes out of scope here

        fx_log_info!("offline_loop: Waiting");

        Ok(futures::future::pending().await)
    }
}
