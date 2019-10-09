// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        location::{all_locations, InspectLocation},
        result::IqueryResult,
    },
    failure::Error,
    futures::future::join_all,
    std::str::FromStr,
};

/// Executes the FIND command.
pub async fn find(paths: &[String]) -> Vec<IqueryResult> {
    let mut locations =
        paths.iter().flat_map(|path| all_locations(path)).collect::<Vec<InspectLocation>>();
    locations.sort();
    let futs = locations.into_iter().map(|location| IqueryResult::try_from(location));
    to_result(join_all(futs).await)
}

/// Executes the CAT command.
pub async fn cat(paths: &[String]) -> Vec<IqueryResult> {
    let mut locations = paths
        .iter()
        .filter_map(|path| InspectLocation::from_str(path).ok())
        .collect::<Vec<InspectLocation>>();
    locations.sort();
    let futs = locations.into_iter().map(|location| IqueryResult::try_from(location));
    let result = to_result(join_all(futs).await);
    result
}

fn to_result(results: Vec<Result<IqueryResult, Error>>) -> Vec<IqueryResult> {
    results
        .into_iter()
        .filter_map(|result| {
            result
                .or_else(|e| {
                    println!("Error: {}", e);
                    Err(e)
                })
                .ok()
        })
        .collect()
}
