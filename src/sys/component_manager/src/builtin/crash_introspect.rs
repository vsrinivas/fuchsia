// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::builtin::capability::BuiltinCapability,
    ::routing::capability_source::InternalCapability,
    anyhow::Error,
    async_trait::async_trait,
    cm_rust::CapabilityName,
    fidl_fuchsia_component as fcomponent, fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    fuchsia_zircon as zx,
    futures::{lock::Mutex, TryStreamExt},
    lazy_static::lazy_static,
    moniker::AbsoluteMoniker,
    std::sync::Arc,
    tracing::*,
};

/// Any stored data is removed after this amount of time
const CLEANUP_DEADLINE_SECONDS: i64 = 600;

lazy_static! {
    static ref CRASH_RECORDS_CAPABILITY_NAME: CapabilityName =
        "fuchsia.sys2.CrashIntrospect".into();
}

#[derive(Debug, Clone, PartialEq)]
pub struct ComponentCrashInfo {
    pub url: String,
    pub moniker: AbsoluteMoniker,
}

impl Into<fsys::ComponentCrashInfo> for ComponentCrashInfo {
    fn into(self) -> fsys::ComponentCrashInfo {
        fsys::ComponentCrashInfo {
            url: Some(self.url),
            moniker: Some(self.moniker.to_string()),
            ..fsys::ComponentCrashInfo::EMPTY
        }
    }
}

#[derive(Debug, Clone, PartialEq)]
pub(crate) struct Record {
    // The point at which this record should be deleted
    deadline: zx::Time,
    // The koid of the thread a crash was observed on
    koid: zx::Koid,
    // The crash information
    crash_info: ComponentCrashInfo,
}

#[derive(Clone)]
pub struct CrashRecords {
    records: Arc<Mutex<Vec<Record>>>,
    _cleanup_task: Arc<fasync::Task<()>>,
}

/// This task will inspect `records`, and sleep until either `records[0].deadline` or for
/// `CLEANUP_DEADLINE_SECONDS` seconds. Upon waking, it removes anything from `records` whose
/// deadline has passed, and then repeats.
///
/// If `records` is not sorted by `Record::deadline` in ascending order then this task will not
/// behave correctly.
async fn record_cleanup_task(records: Arc<Mutex<Vec<Record>>>) {
    loop {
        let sleep_until = {
            let records_guard = records.lock().await;
            if records_guard.is_empty() {
                // If we have no records, then we can sleep for as long as the timeout and check
                // again
                zx::Time::after(zx::Duration::from_seconds(CLEANUP_DEADLINE_SECONDS))
            } else {
                // If there's an upcoming record to delete, sleep until then
                records_guard[0].deadline.clone()
            }
        };
        let timer = fasync::Timer::new(sleep_until);
        timer.await;
        let mut records_guard = records.lock().await;
        while !records_guard.is_empty() && zx::Time::get_monotonic() > records_guard[0].deadline {
            records_guard.remove(0);
        }
    }
}

impl CrashRecords {
    pub fn new() -> Self {
        let records = Arc::new(Mutex::new(vec![]));
        CrashRecords {
            records: records.clone(),
            _cleanup_task: Arc::new(fasync::Task::spawn(record_cleanup_task(records))),
        }
    }

    /// Adds a new report to CrashRecords, and schedules the new reports deletion in
    /// `CLEANUP_DEADLINE_SECONDS`.
    pub async fn add_report(&self, thread_koid: zx::Koid, report: ComponentCrashInfo) {
        self.records.lock().await.push(Record {
            deadline: zx::Time::after(zx::Duration::from_seconds(CLEANUP_DEADLINE_SECONDS)),
            koid: thread_koid.clone(),
            crash_info: report,
        });
    }

    /// Removes the report (if any), and deletes it.
    pub async fn take_report(&self, thread_koid: &zx::Koid) -> Option<ComponentCrashInfo> {
        let mut records_guard = self.records.lock().await;
        let index_to_remove = records_guard
            .iter()
            .enumerate()
            .find(|(_, record)| &record.koid == thread_koid)
            .map(|(i, _)| i);
        if index_to_remove.is_none() {
            warn!("crash introspection failed to provide attribution for the crashed thread with koid {:?}", thread_koid);
        }
        index_to_remove.map(|i| records_guard.remove(i).crash_info)
    }
}

pub struct CrashIntrospectSvc(CrashRecords);

impl CrashIntrospectSvc {
    pub fn new(crash_records: CrashRecords) -> Arc<Self> {
        Arc::new(Self(crash_records))
    }
}

#[async_trait]
impl BuiltinCapability for CrashIntrospectSvc {
    const NAME: &'static str = "CrashIntrospect";
    type Marker = fsys::CrashIntrospectMarker;

    async fn serve(
        self: Arc<Self>,
        mut stream: fsys::CrashIntrospectRequestStream,
    ) -> Result<(), Error> {
        while let Some(request) = stream.try_next().await? {
            match request {
                fsys::CrashIntrospectRequest::FindComponentByThreadKoid {
                    thread_koid,
                    responder,
                } => match self.0.take_report(&zx::Koid::from_raw(thread_koid)).await {
                    Some(report) => responder.send(&mut Ok(report.into()))?,
                    None => responder.send(&mut Err(fcomponent::Error::ResourceNotFound))?,
                },
            }
        }
        Ok(())
    }

    fn matches_routed_capability(&self, capability: &InternalCapability) -> bool {
        capability.matches_protocol(&CRASH_RECORDS_CAPABILITY_NAME)
    }
}

#[cfg(test)]
mod tests {
    use {super::*, fidl::endpoints::create_proxy_and_stream, fuchsia_async as fasync};

    #[fuchsia::test]
    async fn get_crash_report() -> Result<(), Error> {
        let crash_records = CrashRecords::new();
        let crash_records_svc = CrashIntrospectSvc::new(crash_records.clone());

        let (crash_records_proxy, crash_records_stream) =
            create_proxy_and_stream::<fsys::CrashIntrospectMarker>()?;
        let _task = fasync::Task::local(crash_records_svc.serve(crash_records_stream));

        let koid_raw = 123;
        let koid = zx::Koid::from_raw(koid_raw);
        let url = "456".to_string();
        let moniker = AbsoluteMoniker::from(vec!["a"]);
        let crash_report = ComponentCrashInfo { url: url.clone(), moniker: moniker.clone() };

        assert_eq!(
            Err(fcomponent::Error::ResourceNotFound),
            crash_records_proxy.find_component_by_thread_koid(koid_raw).await?
        );

        assert_eq!(None, crash_records.take_report(&koid).await);
        crash_records.add_report(koid, crash_report).await;

        assert_eq!(
            Ok(fsys::ComponentCrashInfo {
                url: Some(url.clone()),
                moniker: Some(moniker.to_string()),
                ..fsys::ComponentCrashInfo::EMPTY
            }),
            crash_records_proxy.find_component_by_thread_koid(koid_raw).await?,
        );

        assert_eq!(
            Err(fcomponent::Error::ResourceNotFound),
            crash_records_proxy.find_component_by_thread_koid(koid_raw).await?
        );
        Ok(())
    }
}
