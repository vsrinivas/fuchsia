// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::work_scheduler::time::TimeSource,
    fidl_fuchsia_sys2 as fsys,
    std::{convert::TryFrom, collections::HashMap, sync::Arc},
};

/// Internal representation of a one-shot or repeating unit of work.
#[derive(Debug, Eq, Ord, PartialEq, PartialOrd)]
struct Work {
    /// Unique identifier for this unit of work (relative to others in `Works` the same collection).
    id: String,
    /// Next deadline for this unit of work, in monotonic time.
    next_deadline_monotonic: i64,
    /// Period between repeating this unit of work (if any), measure in nanoseconds.
    period: Option<i64>,
}

impl TryFrom<(&str, &fsys::WorkRequest)> for Work {
    type Error = fsys::Error;

    fn try_from((id, work_request): (&str, &fsys::WorkRequest))
        -> Result<Self, fsys::Error>
    {
        let next_deadline_monotonic = match &work_request.start {
            None => Err(fsys::Error::InvalidArguments),
            Some(start) => {
                match start {
                    fsys::Start::MonotonicTime(monotonic_time) => Ok(monotonic_time),
                    _ => Err(fsys::Error::InvalidArguments),
                }
            }
        }?;
        Ok(Work {
            id: id.to_string(),
            next_deadline_monotonic: *next_deadline_monotonic,
            period: work_request.period,
        })
    }
}

/// Public representation of a one-shot or repeating unit of work.
#[derive(Debug, Eq, PartialEq)]
pub struct WorkStatus {
    /// Estimated next run time for this unit of work, in monotonic time.
    pub next_run_monotonic_time: i64,
    /// Period between repeating this unit of work (if any), measure in nanoseconds.
    pub period: Option<i64>,
}

impl From<&Work> for WorkStatus {
    fn from(work: &Work) -> Self {
        WorkStatus {
            next_run_monotonic_time: work.next_deadline_monotonic,
            period: work.period,
        }
    }
}

/// Collection of uniquely identifiable `Work` instances with an appropriate interface for
/// `WorkScheduler` to delegate `WorkRequest` <--> `Work` <--> `WorkStatus` conversion and error
/// checking.
#[derive(Clone)]
pub struct Works {
    by_id: HashMap<String, Arc<Work>>,
    time_source: Arc<dyn TimeSource>,
}

impl Works {
    pub fn new(time_source: Arc<dyn TimeSource>) -> Self {
        Works { by_id: HashMap::new(), time_source: time_source }
    }

    /// Insert unit of work into collection.
    /// Errors:
    /// - INSTANCE_ALREADY_EXISTS: Collection already contains work identified by `id`.
    /// - INVALID_ARGUMENTS: Missing or invalid work_request.start value.
    pub fn insert(
        &mut self,
        id: &str,
        work_request: &fsys::WorkRequest,
    ) -> Result<(), fsys::Error> {
        let id_string = id.to_string();
        if self.by_id.contains_key(&id_string) {
            return Err(fsys::Error::InstanceAlreadyExists);
        }

        // Note: This is auto conversion with no signedness checking on durations. That means
        // deadlines in the past are valid and will be serviced via the same algorithm as any other
        // deadline.
        let work = Arc::new(Work::try_from((id, work_request))?);
        self.by_id.insert(id_string, work);

        Ok(())
    }

    /// Remove unit of work from collection.
    /// Errors:
    /// - INSTANCE_NOT_FOUND: There is no `Work` identified by `id` in the collection.
    pub fn delete(&mut self, id: &str) -> Result<(), fsys::Error> {
        match self.by_id.remove(id) {
            Some(_) => Ok(()),
            None => Err(fsys::Error::InstanceNotFound),
        }
    }
}

/// Shared functionality for testing.
#[cfg(test)]
pub mod test {
    use super::*;

    pub fn get_works_status(works: &Works, id: &str) -> Result<WorkStatus, fsys::Error> {
        match works.by_id.get(id) {
            None => Err(fsys::Error::InstanceNotFound),
            Some(work) => {
                let work: &Work = &*work;
                Ok(WorkStatus::from(work))
            },
        }
    }
}

/// Unit tests for `Works`: This is a collection class. Test the consistency of the collection
/// state after performing one or more operations.
#[cfg(test)]
pub mod tests {
    use {
        super::*,
        crate::work_scheduler::time::test::{FakeTimeSource, SECOND},
    };

    #[test]
    fn works_insert_success() {
        let time_source = Arc::new(FakeTimeSource::new());
        let mut works = Works::new(time_source.clone());
        let work_id = "NOW_ONCE";
        let deadline_monotonic = time_source.get_monotonic();
        assert_eq!(
            Ok(()),
            works.insert(
                work_id,
                &fsys::WorkRequest {
                    start: Some(fsys::Start::MonotonicTime(deadline_monotonic)),
                    period: None,
                },
            )
        );

        assert_eq!(1, works.by_id.len());
        let work = works.by_id.get(work_id).expect("by_id should contain work item");
        assert_eq!(time_source.get_monotonic(), work.next_deadline_monotonic);
        assert_eq!(None, work.period);
    }

    #[test]
    fn works_insert_success_exact() {
        let time_source = Arc::new(FakeTimeSource::new());
        let mut works = Works::new(time_source.clone());
        let work_id = "IN_A_SECOND";
        let deadline_monotonic = time_source.get_monotonic() + SECOND;
        assert_eq!(
            Ok(()),
            works.insert(
                work_id,
                &fsys::WorkRequest {
                    start: Some(fsys::Start::MonotonicTime(deadline_monotonic)),
                    period: None,
                }
            )
        );

        assert_eq!(1, works.by_id.len());
        let work = works.by_id.get(work_id).expect("by_id should contain work item");
        assert_eq!(deadline_monotonic, work.next_deadline_monotonic);
        assert_eq!(None, work.period);
    }

    #[test]
    fn works_insert_success_periodic() {
        let time_source = Arc::new(FakeTimeSource::new());
        let mut works = Works::new(time_source.clone());
        let work_id = "EACH_SECOND";
        let deadline_monotonic = time_source.get_monotonic() + SECOND;
        assert_eq!(
            Ok(()),
            works.insert(
                work_id,
                &fsys::WorkRequest {
                    start: Some(fsys::Start::MonotonicTime(deadline_monotonic)),
                    period: Some(SECOND),
                }
            )
        );

        assert_eq!(1, works.by_id.len());
        let work = works.by_id.get(work_id).expect("by_id should contain work item");
        assert_eq!(time_source.get_monotonic() + SECOND, work.next_deadline_monotonic);
        assert_eq!(Some(SECOND), work.period);
    }

    #[test]
    fn works_insert_fail() {
        let mut works = Works::new(Arc::new(FakeTimeSource::new()));
        let err = works.insert("NEVER", &fsys::WorkRequest { start: None, period: None });
        assert_eq!(Err(fsys::Error::InvalidArguments), err);
        assert_eq!(0, works.by_id.len());
    }

    #[test]
    fn works_insert_then_delete_then_lookup() {
        let time_source = Arc::new(FakeTimeSource::new());
        let mut works = Works::new(time_source.clone());
        let work_id = "NOW_ONCE";
        let deadline_monotonic = time_source.get_monotonic();
        assert_eq!(
            Ok(()),
            works.insert(
                work_id,
                &fsys::WorkRequest {
                    start: Some(fsys::Start::MonotonicTime(deadline_monotonic)),
                    period: None,
                },
            )
        );

        assert_eq!(1, works.by_id.len());

        assert_eq!(Ok(()), works.delete(work_id));

        assert_eq!(0, works.by_id.len());
        assert_eq!(Err(fsys::Error::InstanceNotFound), test::get_works_status(&works, work_id));
    }

    // TODO(markdittmer): Make this into a macro to get appropriate line number from test failures.
    fn assert_work_id(works: &Works, expected: &str) {
        assert_eq!(expected, works.by_id.get(expected).expect("Work should exist").id);
    }

    #[test]
    fn works_delete_from_multiple_success() {
        let time_source = Arc::new(FakeTimeSource::new());
        let mut works = Works::new(time_source.clone());
        let now_monotonic = time_source.get_monotonic();
        assert_eq!(
            Ok(()),
            works.insert(
                "NOW_ONCE",
                &fsys::WorkRequest {
                    start: Some(fsys::Start::MonotonicTime(now_monotonic)),
                    period: None,
                },
            )
        );
        assert_eq!(
            Ok(()),
            works.insert(
                "EACH_SECOND",
                &fsys::WorkRequest {
                    start: Some(fsys::Start::MonotonicTime(now_monotonic + SECOND)),
                    period: Some(SECOND),
                },
            )
        );
        assert_eq!(
            Ok(()),
            works.insert(
                "IN_AN_HOUR",
                &fsys::WorkRequest {
                    start: Some(fsys::Start::MonotonicTime(now_monotonic + (SECOND * 60 * 60))),
                    period: None,
                },
            )
        );

        assert_eq!(3, works.by_id.len());
        assert_work_id(&works, "NOW_ONCE");
        assert_work_id(&works, "EACH_SECOND");
        assert_work_id(&works, "IN_AN_HOUR");

        assert_eq!(Ok(()), works.delete("EACH_SECOND"));

        assert_work_id(&works, "NOW_ONCE");
        assert_work_id(&works, "IN_AN_HOUR");

        assert_eq!(None, works.by_id.get("EACH_SECOND"));
    }

    #[test]
    fn works_delete_fail() {
        let mut works = Works::new(Arc::new(FakeTimeSource::new()));
        assert_eq!(Err(fsys::Error::InstanceNotFound), works.delete("DOES_NOT_EXIST"));
    }
}
