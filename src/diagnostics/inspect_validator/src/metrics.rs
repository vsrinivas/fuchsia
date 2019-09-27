// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::Error,
    fuchsia_inspect::{self, format::block::Block, reader as ireader},
    fuchsia_zircon::Vmo,
    serde_derive::Serialize,
    std::{
        self,
        // cmp::min,
        //  collections::{HashMap, HashSet},
        convert::TryFrom,
    },
};

#[derive(Debug, Serialize)]
pub struct Metrics {
    block_count: u64,
    size: u64,
    trial_name: String,
    step_index: usize,
}

impl Metrics {
    pub fn from_vmo(vmo: &Vmo, trial_name: &str, step_index: usize) -> Result<Metrics, Error> {
        let snapshot = ireader::snapshot::Snapshot::try_from(vmo);
        match snapshot {
            Err(e) => Err(e),
            Ok(snapshot) => {
                let mut metrics = Metrics::new(trial_name, step_index);
                for block in snapshot.scan() {
                    metrics.scan(block);
                }
                Ok(metrics)
            }
        }
    }

    fn scan(&mut self, block: Block<&[u8]>) {
        self.block_count += 1;
        self.size += 16 << block.order();
    }

    fn new(trial_name: &str, step_index: usize) -> Metrics {
        Metrics { block_count: 0, size: 0, trial_name: trial_name.into(), step_index }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{puppet, results::Results},
        fuchsia_async as fasync,
    };
    #[fasync::run_singlethreaded(test)]
    async fn metrics_work() -> Result<(), Error> {
        let puppet = puppet::tests::local_incomplete_puppet().await?;
        let metrics = puppet.read_metrics("trialfoo", 42).unwrap();
        let mut results = Results::new();
        results.remember_metrics(metrics);
        assert!(results.to_json().contains("\"trial_name\":\"trialfoo\""));
        assert!(results.to_json().contains(&format!("\"size\":{}", puppet::VMO_SIZE)));
        assert!(results.to_json().contains("\"step_index\":42"));
        assert!(results.to_json().contains("\"block_count\":9"));
        Ok(())
    }
}
