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
pub async fn find(paths: &[String], recursive: bool) -> Vec<Result<IqueryResult, Error>> {
    let futs = paths.iter().map(|path| all_locations(path));
    let mut locations = Vec::<InspectLocation>::new();
    let mut final_results = Vec::<Result<IqueryResult, Error>>::new();
    for result in join_all(futs).await {
        match result {
            Ok(locs) => locations.extend(locs),
            Err(e) => final_results.push(Err(e)),
        }
    }

    locations.sort();

    let results = locations.into_iter().map(|location| IqueryResult::new(location));
    if recursive {
        let futs = results.map(|mut result| async {
            result.load().await?;
            Ok(result)
        });
        final_results.extend(join_all(futs).await);
        final_results
    } else {
        final_results.extend(results.map(|r| Ok(r)).collect::<Vec<_>>());
        final_results
    }
}

/// Executes the CAT command.
pub async fn cat(paths: &[String]) -> Vec<Result<IqueryResult, Error>> {
    let mut locations = paths
        .iter()
        .filter_map(|path| InspectLocation::from_str(path).ok())
        .collect::<Vec<InspectLocation>>();
    locations.sort();
    let futs = locations.into_iter().map(|location| IqueryResult::try_from(location));
    join_all(futs).await
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
        let paths = vec![dir.path().to_string_lossy().to_string()];

        // The result is not loaded when non-recursive.
        let results = find(&paths, false).await;
        assert_eq!(results.len(), 1);
        assert!(!results[0].as_ref().unwrap().is_loaded());

        // Loads the tmp file that contains the inspect hierarchy created above
        // when recursive.
        let results = find(&paths, true).await;
        assert_eq!(results.len(), 1);
        assert!(results[0].as_ref().unwrap().is_loaded());

        Ok(())
    }
}
