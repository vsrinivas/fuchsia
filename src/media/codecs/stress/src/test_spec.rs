// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{elementary_stream::*, output_validator::*, stream::*, stream_runner::*};
use failure::{Error, ResultExt};
use futures::{stream::FuturesUnordered, TryStreamExt};
use std::rc::Rc;

/// A test spec describes all the cases that will run and the circumstances in which
/// they will run.
pub struct TestSpec {
    pub cases: Vec<TestCase>,
    pub relation: CaseRelation,
}

/// A case relation describes the temporal relationship between two test cases.
pub enum CaseRelation {
    /// With serial relation, test cases will be run in sequence using the same codec server.
    Serial,
    /// With concurrent relation, test cases will run concurrently using two codec servers.
    #[allow(unused)]
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
    pub async fn run(self) -> Result<(), Error> {
        match self.relation {
            CaseRelation::Serial => await!(run_cases_serially(self.cases)),
            CaseRelation::Concurrent => await!(run_cases_concurrently(self.cases)),
        }
    }
}

async fn run_cases_serially(cases: Vec<TestCase>) -> Result<(), Error> {
    let mut stream_runner = StreamRunner::new();

    for case in cases {
        let output =
            await!(stream_runner.run_stream(case.stream, case.stream_options.unwrap_or_default()))
                .context(format!("Running case {}", case.name))?;
        for validator in case.validators {
            validator.validate(&output).context(format!("Validating case {}", case.name))?;
        }
    }

    Ok(())
}

async fn run_cases_concurrently(cases: Vec<TestCase>) -> Result<(), Error> {
    let mut unordered = FuturesUnordered::new();
    for case in cases {
        unordered.push(run_cases_serially(vec![case]))
    }

    while let Some(()) = await!(unordered.try_next())? {}

    Ok(())
}
