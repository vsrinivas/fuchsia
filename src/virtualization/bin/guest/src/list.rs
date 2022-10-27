// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {crate::arguments, crate::services, anyhow::Error};

pub async fn handle_list(args: &arguments::ListArgs) -> Result<String, Error> {
    match args.guest_type {
        Some(guest_type) => {
            let manager = services::connect_to_manager(guest_type)?;
            guest_cli::list::get_detailed_information(guest_type, manager).await
        }
        None => {
            let mut managers = Vec::new();
            for guest_type in arguments::GuestType::all_guests() {
                let manager = services::connect_to_manager(guest_type)?;
                managers.push((guest_type.to_string(), manager));
            }
            guest_cli::list::get_enviornment_summary(managers).await
        }
    }
}
