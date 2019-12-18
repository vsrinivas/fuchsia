// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    elementary_stream::*, output_validator::*, stream::*, stream_runner::*, FatalError, Result,
};
use failure::ResultExt;
use fidl_fuchsia_media::StreamProcessorProxy;
use futures::{future::BoxFuture, stream::FuturesUnordered, TryStreamExt};
use std::rc::Rc;

const FIRST_FORMAT_DETAILS_VERSION_ORDINAL: u64 = 1;

pub trait StreamProcessorFactory {
    fn connect_to_stream_processor(
        &self,
        stream: &dyn ElementaryStream,
        format_details_version_ordinal: u64,
    ) -> BoxFuture<'_, Result<StreamProcessorProxy>>;
}

/// A test spec describes all the cases that will run and the circumstances in which
/// they will run.
pub struct TestSpec {
    pub cases: Vec<TestCase>,
    pub relation: CaseRelation,
    pub stream_processor_factory: Rc<dyn StreamProcessorFactory>,
}

/// A case relation describes the temporal relationship between two test cases.
pub enum CaseRelation {
    /// With serial relation, test cases will be run in sequence using the same codec server.
    Serial,
    /// With concurrent relation, test cases will run concurrently using two or more codec servers.
    Concurrent,
}

/// A test cases describes a sequence of elementary stream chunks that should be fed into a codec
/// server, and a set of validators to check the output. To pass, all validations must pass for all
/// output from the stream.
pub struct TestCase {
    pub name: &'static str,
    pub stream: Rc<dyn ElementaryStream>,
    pub validators: Vec<Rc<dyn OutputValidator>>,
    pub stream_options: Option<StreamOptions>,
}

impl TestSpec {
    pub async fn run(self) -> Result<()> {
        match self.relation {
            CaseRelation::Serial => {
                run_cases_serially(self.stream_processor_factory.as_ref(), self.cases).await
            }
            CaseRelation::Concurrent => {
                run_cases_concurrently(self.stream_processor_factory.as_ref(), self.cases).await
            }
        }
    }
}

async fn run_cases_serially(
    stream_processor_factory: &dyn StreamProcessorFactory,
    cases: Vec<TestCase>,
) -> Result<()> {
    let stream_processor =
        if let Some(stream) = cases.first().as_ref().map(|case| case.stream.as_ref()) {
            stream_processor_factory
                .connect_to_stream_processor(stream, FIRST_FORMAT_DETAILS_VERSION_ORDINAL)
                .await?
        } else {
            return Err(FatalError(String::from("No test cases provided.")).into());
        };
    let mut stream_runner = StreamRunner::new(stream_processor);

    for case in cases {
        let output = stream_runner
            .run_stream(case.stream, case.stream_options.unwrap_or_default())
            .await
            .context(format!("Running case {}", case.name))?;
        for validator in case.validators {
            validator.validate(&output).context(format!("Validating case {}", case.name))?;
        }
    }

    Ok(())
}

async fn run_cases_concurrently(
    stream_processor_factory: &dyn StreamProcessorFactory,
    cases: Vec<TestCase>,
) -> Result<()> {
    let mut unordered = FuturesUnordered::new();
    for case in cases {
        unordered.push(run_cases_serially(stream_processor_factory, vec![case]))
    }

    while let Some(()) = unordered.try_next().await? {}

    Ok(())
}

pub fn with_large_stack(f: fn() -> Result<()>) -> Result<()> {
    // The TestSpec futures are too big to fit on Fuchsia's default stack.
    // We need this when running the test in panic=abort mode (in unwind mode,
    // the test harness creates a thread with a 2MB stack for us).
    const STACK_SIZE: usize = 2 * 1024 * 1024;
    std::thread::Builder::new().stack_size(STACK_SIZE).spawn(f).unwrap().join().unwrap()
}
