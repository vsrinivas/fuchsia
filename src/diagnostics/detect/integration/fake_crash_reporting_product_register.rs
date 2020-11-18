// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::DoneSignaler,
    anyhow::{bail, Error},
    async_trait::async_trait,
    fidl_fuchsia_feedback as fcrash,
    futures::StreamExt,
    log::*,
    std::sync::{
        atomic::{AtomicUsize, Ordering},
        Arc,
    },
    test_utils_lib::injectors::ProtocolInjector,
};

const REPORT_PROGRAM_NAME: &str = "triage_detect";
const REGISTER_PRODUCT_NAME: &str = "FuchsiaDetect";

/// FakeCrashReportingProductRegister can be injected to capture Detect's program-name registration.
pub struct FakeCrashReportingProductRegister {
    done_signaler: DoneSignaler,
    // Since upsert() is a oneway call, the program will keep running after it's sent. So if it's
    // sent to the TestEventSender stream, it will appear in an unpredictable place - or if the
    // program bails, it may not appear at all even if it happened.
    //
    // The best information we can get is: Did it happen never, once, or more than once? If it
    // happened, did it happen correctly each time?
    //
    // The program under test should call upsert at most once, with predictable inputs. So the
    // correctness can be tracked with an AtomicUsize which will have the following values:
    // 0: No call to upsert().
    // 1: A single correct call to upsert().
    // >1: Error: Multiple calls, and/or incorrect call.
    ok_tracker: AtomicUsize,
}

impl FakeCrashReportingProductRegister {
    pub fn new(done_signaler: DoneSignaler) -> Arc<FakeCrashReportingProductRegister> {
        Arc::new(FakeCrashReportingProductRegister {
            done_signaler,
            ok_tracker: AtomicUsize::new(0),
        })
    }

    fn record_correct_registration(&self) {
        self.ok_tracker.fetch_add(1, Ordering::Relaxed);
    }

    fn record_bad_registration(&self) {
        info!("Detected bad registration");
        self.ok_tracker.fetch_add(2, Ordering::Relaxed);
    }

    pub fn detected_error(&self) -> bool {
        let state = self.ok_tracker.load(Ordering::Relaxed);
        state > 1
    }

    pub fn detected_good_registration(&self) -> bool {
        let state = self.ok_tracker.load(Ordering::Relaxed);
        state == 1
    }
}

fn evaluate_registration(
    program_name: String,
    product: &fcrash::CrashReportingProduct,
) -> Result<(), Error> {
    let fcrash::CrashReportingProduct { name: product_name, .. } = product;
    if product_name != &Some(REGISTER_PRODUCT_NAME.to_string()) {
        bail!(
            "Crash report program name should be {} but it was {:?}",
            REGISTER_PRODUCT_NAME,
            product_name
        );
    }
    if &program_name != REPORT_PROGRAM_NAME {
        bail!("program_name should be {} but it was {:?}", REGISTER_PRODUCT_NAME, program_name);
    }
    Ok(())
}

#[async_trait]
impl ProtocolInjector for FakeCrashReportingProductRegister {
    type Marker = fcrash::CrashReportingProductRegisterMarker;

    async fn serve(
        self: Arc<Self>,
        mut request_stream: fcrash::CrashReportingProductRegisterRequestStream,
    ) -> Result<(), Error> {
        loop {
            match request_stream.next().await {
                Some(Ok(fcrash::CrashReportingProductRegisterRequest::Upsert {
                    component_url,
                    product,
                    ..
                })) => match evaluate_registration(component_url, &product) {
                    Ok(()) => {
                        self.record_correct_registration();
                    }
                    Err(problem) => {
                        error!("Problem in report: {}", problem);
                        self.record_bad_registration();
                        self.done_signaler.signal_done().await;
                    }
                },
                Some(Err(e)) => {
                    info!("Registration error: {}", e);
                    self.record_bad_registration();
                    self.done_signaler.signal_done().await;
                    bail!("{}", e);
                }
                None => break,
            }
        }
        Ok(())
    }
}
