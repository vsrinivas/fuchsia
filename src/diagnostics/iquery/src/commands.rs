// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        location::{all_locations, InspectLocation},
        result::IqueryResult,
    },
    anyhow::Error,
    futures::future::join_all,
    std::str::FromStr,
};

/// Executes the FIND command.
pub async fn find(paths: &[String], recursive: bool) -> Vec<IqueryResult> {
    let mut locations =
        paths.iter().flat_map(|path| all_locations(path)).collect::<Vec<InspectLocation>>();
    locations.sort();
    let results = locations.into_iter().map(|location| IqueryResult::new(location));
    if recursive {
        let futs = results.map(|mut result| async {
            result.load().await?;
            Ok(result)
        });
        to_result(join_all(futs).await)
    } else {
        results.collect()
    }
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

#[cfg(test)]
mod tests {
    use {
        super::*, fuchsia_async as fasync, fuchsia_inspect::component, std::fs, tempfile::tempdir,
    };

    #[fasync::run_singlethreaded(test)]
    async fn test_find() -> Result<(), Error> {
        let dir = tempdir().unwrap();
        let file_path = dir.path().join("root.inspect");

        // Write some inspect data to a tmp file.
        let data = component::inspector().copy_vmo_data().unwrap();
        fs::write(file_path, &data).unwrap();
        let paths = vec!["/tmp".to_string()];

        // The result is not loaded when non-recursive.
        let results = find(&paths, false).await;
        assert_eq!(results.len(), 1);
        assert!(!results[0].is_loaded());

        // Loads the tmp file that contains the inspect hierarchy created above
        // when recursive.
        let results = find(&paths, true).await;
        assert_eq!(results.len(), 1);
        assert!(results[0].is_loaded());

        Ok(())
    }
}
