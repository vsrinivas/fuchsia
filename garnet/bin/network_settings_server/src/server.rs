// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::net_settings_types::*;

use {
    anyhow::{format_err, Error},
    fuchsia_syslog::{fx_log_err, fx_log_info},
    rouille,
    rouille::{router, Request, Response},
    serde::de::DeserializeOwned,
    serde_json,
    std::sync::Mutex,
};

#[derive(Debug, Clone)]
struct SettingsState {
    wan_config: Option<WANConfiguration>,
    dhcp_config: Option<DHCPConfiguration>,
    ap_config: Option<APConfiguration>,
}

impl Default for SettingsState {
    fn default() -> SettingsState {
        SettingsState {
            wan_config: Some(WANConfiguration {
                addr: NetAddress::V4([0, 0, 0, 0], 32),
                dynamically_assigned: false,
            }),
            // TODO(fxbug.dev/29244): Secure defaults for this.
            dhcp_config: Some(DHCPConfiguration {
                dns_server_addr: NetAddress::V4([8, 8, 8, 8], 32),
                netmask: NetAddress::V4([192, 168, 41, 0], 24),
                addr_range: (
                    NetAddress::V4([192, 168, 41, 1], 32),
                    NetAddress::V4([192, 168, 41, 255], 32),
                ),
            }),
            // TODO(fxbug.dev/29244): Secure defaults for this.
            ap_config: Some(APConfiguration {
                ssid: String::from("Basking Rootwalla").into_bytes(),
                password: String::from(""),
            }),
        }
    }
}

struct SettingsServer {
    page: String,
    current_settings: SettingsState,
}

impl SettingsServer {
    fn new(frontend_webpage: String) -> SettingsServer {
        SettingsServer { page: frontend_webpage, current_settings: SettingsState::default() }
    }

    // TODO(fxbug.dev/29233): Hook up to wlancfg FIDL endpoints.
    fn set_wan_config(&mut self, config: WANConfiguration) {
        self.current_settings.wan_config = Some(config);
    }

    fn get_wan_config(&self) -> Option<WANConfiguration> {
        self.current_settings.wan_config.as_ref().cloned()
    }

    // TODO(fxbug.dev/29233): Hook up to netcfg FIDL endpoints.
    fn set_dhcp_config(&mut self, config: DHCPConfiguration) {
        self.current_settings.dhcp_config = Some(config);
    }

    fn get_dhcp_config(&self) -> Option<DHCPConfiguration> {
        self.current_settings.dhcp_config.as_ref().cloned()
    }

    // TODO(fxbug.dev/29233): Hook up to wlancfg FIDL endpoints.
    fn set_ap_config(&mut self, config: APConfiguration) {
        self.current_settings.ap_config = Some(config);
    }

    fn get_ap_config(&self) -> Option<APConfiguration> {
        self.current_settings.ap_config.as_ref().cloned()
    }
}

pub fn start_server(address: String, webpage: String) -> ! {
    let server = Mutex::new(SettingsServer::new(webpage));
    rouille::start_server(&address[..], move |request| {
        let mut settings = &mut server.lock().unwrap();
        handle_client_request(&mut settings, request)
    });
}

fn parse_request<T: DeserializeOwned>(request: &Request) -> Result<T, Error> {
    let data = match request.data() {
        Some(d) => d,
        None => return Err(format_err!("Failed to parse request: no data")),
    };

    Ok(serde_json::from_reader(data)?)
}

fn success() -> Response {
    Response::text("") // Empty success
}

fn handle_client_request(server: &mut SettingsServer, request: &Request) -> Response {
    router!(request,
        (GET) (/) => {
            fx_log_info!(tag: "server", "Received GET for index page.");
            Response::html(server.page.clone())
        },
        (GET) (/wan_info) => {
            fx_log_info!(tag: "server", "Received GET for wan_info.");

            match serde_json::to_string(&server.get_wan_config()) {
                Ok(data) => {
                    Response::from_data("application/json", data.into_bytes())
                },
                Err(e) => {
                    fx_log_err!(tag: "server", "Couldn't create response! Error: {}", e);
                    Response::text("").with_status_code(500)
                },
            }
        },
        (PUT) (/wan_info) => {
            fx_log_info!(tag: "server", "Received PUT for wan_info.");

            let wan_info = match parse_request(request) {
                Ok(info) => info,
                Err(e) => {
                    fx_log_err!(tag: "server", "Bad Request: {}", e);
                    return Response::empty_400();
                },
            };

            server.set_wan_config(wan_info);
            success()
        },
        (GET) (/ssid_info) => {
            fx_log_info!(tag: "server", "Received GET for ssid_info.");
            match serde_json::to_string(&server.get_ap_config()) {
                Ok(data) => {
                    Response::from_data("application/json", data.into_bytes())
                },
                Err(e) => {
                    fx_log_err!(tag: "server", "Couldn't create response! Error: {}", e);
                    Response::text("").with_status_code(500)
                },
            }
        },
        (PUT) (/ssid_info) => {
            fx_log_info!(tag: "server", "Received PUT for ssid_info.");
            let ssid_info = match parse_request(request) {
                Ok(info) => info,
                Err(e) => {
                    fx_log_err!(tag: "server", "Bad Request: {}", e);
                    return Response::empty_400();
                },
            };

            server.set_ap_config(ssid_info);
            success()
        },
        (GET) (/dhcp_info) => {
            fx_log_info!(tag: "server", "Received GET for dhcp_info.");
            match serde_json::to_string(&server.get_dhcp_config()) {
                Ok(data) => {
                    Response::from_data("application/json", data.into_bytes())
                },
                Err(e) => {
                    fx_log_err!(tag: "server", "Couldn't create response! Error: {}", e);
                    Response::text("").with_status_code(500)
                },
            }
        },
        (PUT) (/dhcp_info) => {
            fx_log_info!(tag: "server", "Received PUT for dhcp_info.");
            let dhcp_info = match parse_request(request) {
                Ok(info) => info,
                Err(e) => {
                    fx_log_err!(tag: "server", "Bad Request: {}", e);
                    return Response::empty_400();
                },
            };

            server.set_dhcp_config(dhcp_info);
            success()
        },
        _ => {
            fx_log_err!(tag: "server", "Received unknown request.");
            Response::empty_404()
        }
    )
}
