// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::output::{
    noop::NoopDirectoryWriter, ArtifactType, DirectoryArtifactType, DynArtifact,
    DynDirectoryArtifact, EntityId, ReportedOutcome, Reporter, Timestamp,
};
use async_trait::async_trait;
use fuchsia_async as fasync;
use log::error;
use parking_lot::Mutex;
use std::{
    collections::HashMap,
    io::{Error, Write},
    sync::{atomic::AtomicU32, Arc},
    time::Duration,
};

/// Duration after which to emit an excessive duration log.
const EXCESSIVE_DURATION: Duration = Duration::from_secs(60);

/// A handle around an inner writer. This serves as a "multiplexing" writer that
/// writes bytes from multiple sources into a single serial destination, typically
/// to stdout.
/// Output sent to a handle is buffered until a newline is encountered, then the
/// buffered output is written to the inner writer.
/// The handle also supports prepending a prefix to the start of each buffer. This
/// helps preserve existing behavior where prefixes are added to the start of stdout
/// and log lines to help a developer understand what produced some output.
struct ShellWriterHandle<W: 'static + Write + Send + Sync> {
    inner: Arc<Mutex<ShellWriterHandleInner<W>>>,
    buffer: Vec<u8>,
    /// Prefix, if any, to prepend to output before writing to the inner writer.
    prefix: Option<Vec<u8>>,
    handle_id: u32,
}

impl<W: 'static + Write + Send + Sync> ShellWriterHandle<W> {
    const NEWLINE_BYTE: u8 = b'\n';
    const BUFFER_CAPACITY: usize = 1024;

    /// Create a new handle to a wrapped writer.
    fn new_handle(inner: Arc<Mutex<ShellWriterHandleInner<W>>>, prefix: Option<String>) -> Self {
        let mut lock = inner.lock();
        let handle_id = lock.num_handles;
        lock.num_handles += 1;
        drop(lock);
        Self {
            inner,
            buffer: Vec::with_capacity(Self::BUFFER_CAPACITY),
            prefix: prefix.map(String::into_bytes),
            handle_id,
        }
    }

    /// Write a full line to the inner writer.
    fn write_bufs(writer: &mut W, bufs: &[&[u8]]) -> Result<(), Error> {
        for buf in bufs {
            writer.write_all(buf)?;
        }
        Ok(())
    }
}

/// Inner mutable state for |ShellWriterHandle|.
struct ShellWriterHandleInner<W: 'static + Write + Send + Sync> {
    /// The writer to which all content is passed.
    writer: W,
    /// The id of the last handle that wrote to the writer, used to conditionally
    /// output a prefix only when the handle writing to the output changes.
    last_writer_id: Option<u32>,
    /// The number of handles that have been created. Used to assign ids to handles.
    num_handles: u32,
}

impl<W: 'static + Write + Send + Sync> ShellWriterHandleInner<W> {
    fn new(writer: W) -> Self {
        Self { writer, last_writer_id: None, num_handles: 0 }
    }
}

/// A handle to a writer contained in a |ShellWriterHandle|. This is exposed for testing
/// purposes.
pub struct ShellWriterView<W: 'static + Write + Send + Sync>(Arc<Mutex<ShellWriterHandleInner<W>>>);

impl<W: 'static + Write + Send + Sync> ShellWriterView<W> {
    pub fn lock(&self) -> parking_lot::MappedMutexGuard<'_, W> {
        parking_lot::MutexGuard::map(self.0.lock(), |handle_inner| &mut handle_inner.writer)
    }
}

impl<W: 'static + Write + Send + Sync> Write for ShellWriterHandle<W> {
    fn write(&mut self, buf: &[u8]) -> Result<usize, Error> {
        // find the last newline in the buffer. In case multiple lines are written as once,
        // we should write once to the inner writer and add our prefix only once. This helps
        // avoid spamming the output with prefixes in case many lines are present.
        let newline_pos = buf
            .iter()
            .rev()
            .position(|byte| *byte == Self::NEWLINE_BYTE)
            .map(|pos_from_end| buf.len() - pos_from_end - 1);
        // In case we'd exceed the buffer, just wrte everything, but append a newline to avoid
        // interspersing.
        let (final_byte_pos, append_newline) = match newline_pos {
            // no newline, pushing all to buffer would exceed capacity
            None if self.buffer.len() + buf.len() > Self::BUFFER_CAPACITY => (buf.len() - 1, true),
            None => {
                self.buffer.extend_from_slice(buf);
                return Ok(buf.len());
            }
            // newline exists, but the rest of buf would exceed capacity.
            Some(pos) if buf.len() - pos > Self::BUFFER_CAPACITY => (buf.len() - 1, true),
            Some(pos) => (pos, false),
        };

        let mut inner = self.inner.lock();
        let last_writer_id = inner.last_writer_id.replace(self.handle_id);

        let mut bufs_to_write = vec![];
        if let Some(prefix) = self.prefix.as_ref() {
            if last_writer_id != Some(self.handle_id) {
                bufs_to_write.push(prefix.as_slice());
            }
        }
        if !self.buffer.is_empty() {
            bufs_to_write.push(self.buffer.as_slice());
        }
        bufs_to_write.push(&buf[..final_byte_pos + 1]);
        if append_newline {
            bufs_to_write.push(&[Self::NEWLINE_BYTE]);
        }

        Self::write_bufs(&mut inner.writer, bufs_to_write.as_slice())?;

        self.buffer.clear();
        self.buffer.extend_from_slice(&buf[final_byte_pos + 1..]);
        Ok(buf.len())
    }

    fn flush(&mut self) -> Result<(), Error> {
        let mut inner = self.inner.lock();
        let last_writer_id = inner.last_writer_id.replace(self.handle_id);
        if !self.buffer.is_empty() {
            self.buffer.push(Self::NEWLINE_BYTE);
            let mut bufs_to_write = vec![];
            if let Some(prefix) = self.prefix.as_ref() {
                if last_writer_id != Some(self.handle_id) {
                    bufs_to_write.push(prefix.as_slice());
                }
            }
            bufs_to_write.push(self.buffer.as_slice());

            Self::write_bufs(&mut inner.writer, bufs_to_write.as_slice())?;
            self.buffer.clear();
        }
        inner.writer.flush()
    }
}

impl<W: 'static + Write + Send + Sync> std::ops::Drop for ShellWriterHandle<W> {
    fn drop(&mut self) {
        let _ = self.flush();
    }
}

/// A reporter that outputs results and artifacts to a single stream, usually stdout.
/// This reporter is intended to provide "live" updates to a developer watching while
/// tests are executed.
pub struct ShellReporter<W: 'static + Write + Send + Sync> {
    /// Arc around the writer and state, used to dispense more handles.
    inner: Arc<Mutex<ShellWriterHandleInner<W>>>,
    /// Map containing known information about each entity.
    entity_state_map: Mutex<HashMap<EntityId, EntityState>>,
    /// Number of completed suites, used to output
    completed_suites: AtomicU32,
}

/// All known state needed by a |ShellReporter| to display results.
struct EntityState {
    name: String,
    excessive_duration_task: Option<fasync::Task<()>>,
    children: Vec<EntityId>,
    restricted_logs: Option<ShellWriterView<Vec<u8>>>,
    run_state: EntityRunningState,
}

enum EntityRunningState {
    NotRunning,
    Started,
    Finished(ReportedOutcome),
}

impl EntityState {
    fn new<S: Into<String>>(name: S) -> Self {
        Self {
            name: name.into(),
            excessive_duration_task: None,
            children: vec![],
            restricted_logs: None,
            run_state: EntityRunningState::NotRunning,
        }
    }

    fn name(&self) -> &str {
        &self.name
    }
}

impl ShellReporter<Vec<u8>> {
    pub fn new_expose_writer_for_test() -> (Self, ShellWriterView<Vec<u8>>) {
        let inner = Arc::new(Mutex::new(ShellWriterHandleInner::new(vec![])));
        let mut entity_state_map = HashMap::new();
        entity_state_map.insert(EntityId::TestRun, EntityState::new("TEST RUN"));
        (
            Self {
                inner: inner.clone(),
                entity_state_map: Mutex::new(entity_state_map),
                completed_suites: AtomicU32::new(0),
            },
            ShellWriterView(inner),
        )
    }
}

impl<W: 'static + Write + Send + Sync> ShellReporter<W> {
    pub fn new(inner: W) -> Self {
        let inner = Arc::new(Mutex::new(ShellWriterHandleInner::new(inner)));
        let mut entity_state_map = HashMap::new();
        entity_state_map.insert(EntityId::TestRun, EntityState::new("TEST RUN"));
        Self {
            inner,
            entity_state_map: Mutex::new(entity_state_map),
            completed_suites: AtomicU32::new(0),
        }
    }

    fn new_writer_handle(&self, prefix: Option<String>) -> Box<DynArtifact> {
        Box::new(ShellWriterHandle::new_handle(Arc::clone(&self.inner), prefix))
    }

    fn num_known_suites(entity_map: &HashMap<EntityId, EntityState>) -> usize {
        entity_map.keys().filter(|id| matches!(id, EntityId::Suite(_))).count()
    }
}

#[async_trait]
impl<W: 'static + Write + Send + Sync> Reporter for ShellReporter<W> {
    async fn new_entity(&self, entity: &EntityId, name: &str) -> Result<(), Error> {
        let mut map = self.entity_state_map.lock();
        map.insert(entity.clone(), EntityState::new(name));
        if let EntityId::Case { suite, .. } = entity {
            map.get_mut(&EntityId::Suite(*suite)).unwrap().children.push(entity.clone());
        }
        Ok(())
    }

    async fn entity_started(&self, entity: &EntityId, _: Timestamp) -> Result<(), Error> {
        let mut writer = self.new_writer_handle(None);
        let mut entity_map_lock = self.entity_state_map.lock();
        let mut entity_entry = entity_map_lock.get_mut(entity).unwrap();
        entity_entry.run_state = EntityRunningState::Started;
        let name = entity_entry.name().to_string();
        match entity {
            EntityId::TestRun => (),
            EntityId::Suite(_) => writeln!(writer, "Running test '{}'", name)?,
            EntityId::Case { .. } => {
                writeln!(writer, "[RUNNING]\t{}", name)?;
                entity_entry.excessive_duration_task = Some(fasync::Task::spawn(async move {
                    fasync::Timer::new(EXCESSIVE_DURATION).await;
                    writeln!(
                        writer,
                        "[duration - {}]:\tStill running after {:?} seconds",
                        name,
                        EXCESSIVE_DURATION.as_secs()
                    )
                    .unwrap_or_else(|e| error!("Failed to write: {:?}", e));
                }));
            }
        }
        Ok(())
    }

    async fn entity_stopped(
        &self,
        entity: &EntityId,
        outcome: &ReportedOutcome,
        _: Timestamp,
    ) -> Result<(), Error> {
        let mut writer = self.new_writer_handle(None);
        let mut entity_map_lock = self.entity_state_map.lock();
        entity_map_lock.get_mut(entity).unwrap().run_state = EntityRunningState::Finished(*outcome);
        let entity_entry = entity_map_lock.get_mut(entity).unwrap();
        let name = entity_entry.name().to_string();

        // cancel any tasks for reporting excessive duration
        let _ = entity_entry.excessive_duration_task.take();
        match entity {
            EntityId::TestRun => (),
            EntityId::Suite(_) => (),
            EntityId::Case { .. } => {
                // We don't list error result as it indicates the test didn't finish.
                if *outcome != ReportedOutcome::Error {
                    writeln!(writer, "[{}]\t{}", outcome, name)?;
                }
            }
        }
        Ok(())
    }

    async fn entity_finished(&self, entity: &EntityId) -> Result<(), Error> {
        let mut writer = self.new_writer_handle(None);
        let entity_map_lock = self.entity_state_map.lock();
        let entity_entry = entity_map_lock.get(entity).unwrap();
        let name = entity_entry.name().to_string();
        let outcome = match &entity_entry.run_state {
            EntityRunningState::Finished(outcome) => *outcome,
            _ => ReportedOutcome::Inconclusive,
        };
        let children: Vec<_> = entity_entry.children.iter().cloned().collect();
        match entity {
            EntityId::TestRun => (),
            EntityId::Suite(_) => {
                if matches!(entity_entry.run_state, EntityRunningState::NotRunning) {
                    // no need to output a report if the test wasn't even started.
                    return Ok(());
                }

                let cases: Vec<_> =
                    children.iter().map(|child| entity_map_lock.get(child).unwrap()).collect();
                let executed: Vec<_> = cases
                    .iter()
                    .filter(|case| match &case.run_state {
                        EntityRunningState::Finished(_) | EntityRunningState::Started => true,
                        EntityRunningState::NotRunning => false,
                    })
                    .collect();
                let mut failed: Vec<_> = cases
                    .iter()
                    .filter(|case| {
                        matches!(
                            &case.run_state,
                            EntityRunningState::Finished(
                                ReportedOutcome::Failed | ReportedOutcome::Timedout
                            )
                        )
                    })
                    .map(|case| case.name())
                    .collect();
                failed.sort();
                let mut not_finished: Vec<_> = cases
                    .iter()
                    .filter(|case| {
                        matches!(
                            &case.run_state,
                            EntityRunningState::Started
                                | EntityRunningState::Finished(ReportedOutcome::Error)
                        )
                    })
                    .map(|case| case.name())
                    .collect();
                not_finished.sort();
                let num_passed = cases
                    .iter()
                    .filter(|case| {
                        matches!(
                            &case.run_state,
                            EntityRunningState::Finished(ReportedOutcome::Passed)
                        )
                    })
                    .count();

                let suite_number =
                    self.completed_suites.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
                if suite_number > 0 {
                    writeln!(
                        writer,
                        "\nTest run count {}/{}",
                        suite_number + 1,
                        Self::num_known_suites(&*entity_map_lock),
                    )?;
                }
                writeln!(writer)?;
                if !failed.is_empty() {
                    writeln!(writer, "Failed tests: {}", failed.join(", "))?;
                }
                if !not_finished.is_empty() {
                    writeln!(writer, "\nThe following test(s) never completed:")?;
                    for t in not_finished {
                        writeln!(writer, "{}", t)?;
                    }
                }
                writeln!(writer, "{} out of {} tests passed...", num_passed, executed.len())?;

                if let Some(restricted_logs) = &entity_entry.restricted_logs {
                    writeln!(writer, "\nTest {} produced unexpected high-severity logs:", &name)?;
                    writeln!(writer, "----------------xxxxx----------------")?;
                    writer.write_all(restricted_logs.lock().as_slice())?;
                    writeln!(writer, "\n----------------xxxxx----------------")?;
                    writeln!(writer, "Failing this test. See: https://fuchsia.dev/fuchsia-src/concepts/testing/logs#restricting_log_severity\n")?;
                }
                match outcome {
                    ReportedOutcome::Cancelled => {
                        writeln!(writer, "{} was cancelled before completion.", &name)?
                    }
                    ReportedOutcome::DidNotFinish => {
                        writeln!(writer, "{} did not complete successfully.", &name)?
                    }
                    other => writeln!(writer, "{} completed with result: {}", &name, other)?,
                }
            }
            EntityId::Case { .. } => (),
        }
        Ok(())
    }

    async fn new_artifact(
        &self,
        entity: &EntityId,
        artifact_type: &ArtifactType,
    ) -> Result<Box<DynArtifact>, Error> {
        let mut lock = self.entity_state_map.lock();
        let mut entity = lock.get_mut(entity).unwrap();
        let name = entity.name();

        Ok(match artifact_type {
            ArtifactType::Stdout => self.new_writer_handle(format!("[stdout - {}]\n", name).into()),
            ArtifactType::Stderr => self.new_writer_handle(format!("[stderr - {}]\n", name).into()),
            ArtifactType::Syslog => self.new_writer_handle(None),
            ArtifactType::RestrictedLog => {
                // Restricted logs are saved for reporting when the entity completes.
                let log_buffer = Arc::new(Mutex::new(ShellWriterHandleInner::new(vec![])));
                entity.restricted_logs = Some(ShellWriterView(log_buffer.clone()));
                Box::new(ShellWriterHandle::new_handle(log_buffer, None))
            }
        })
    }

    async fn new_directory_artifact(
        &self,
        _entity: &EntityId,
        _artifact_type: &DirectoryArtifactType,
        _component_moniker: Option<String>,
    ) -> Result<Box<DynDirectoryArtifact>, Error> {
        // For the purpose of live reporting we don't display anything from a directory.
        Ok(Box::new(NoopDirectoryWriter))
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::output::{CaseId, RunReporter, SuiteId};
    use std::io::ErrorKind;

    fn create_writer_inner_and_view(
    ) -> (Arc<Mutex<ShellWriterHandleInner<Vec<u8>>>>, ShellWriterView<Vec<u8>>) {
        let inner = Arc::new(Mutex::new(ShellWriterHandleInner::new(vec![])));
        (inner.clone(), ShellWriterView(inner))
    }

    #[fuchsia::test]
    fn single_handle() {
        let (handle_inner, output) = create_writer_inner_and_view();
        let mut write_handle = ShellWriterHandle::new_handle(handle_inner, None);

        assert_eq!(write_handle.write(b"hello world").unwrap(), b"hello world".len(),);
        assert!(output.lock().is_empty());

        assert_eq!(write_handle.write(b"\n").unwrap(), b"\n".len(),);
        assert_eq!(output.lock().as_slice(), b"hello world\n");

        assert_eq!(write_handle.write(b"flushed output").unwrap(), b"flushed output".len(),);
        write_handle.flush().unwrap();
        assert_eq!(output.lock().as_slice(), b"hello world\nflushed output\n");
    }

    #[fuchsia::test]
    fn single_handle_with_prefix() {
        let (handle_inner, output) = create_writer_inner_and_view();
        let mut write_handle =
            ShellWriterHandle::new_handle(handle_inner, Some("[prefix] ".to_string()));

        assert_eq!(write_handle.write(b"hello world").unwrap(), b"hello world".len(),);
        assert!(output.lock().is_empty());

        assert_eq!(write_handle.write(b"\n").unwrap(), b"\n".len(),);
        assert_eq!(output.lock().as_slice(), b"[prefix] hello world\n");

        assert_eq!(write_handle.write(b"flushed output").unwrap(), b"flushed output".len(),);
        write_handle.flush().unwrap();
        assert_eq!(output.lock().as_slice(), b"[prefix] hello world\nflushed output\n");
    }

    #[fuchsia::test]
    fn single_handle_multiple_line() {
        let (handle_inner, output) = create_writer_inner_and_view();
        let mut write_handle = ShellWriterHandle::new_handle(handle_inner, None);
        const WRITE_BYTES: &[u8] = b"This is a \nmultiline output \nwithout newline termination";
        assert_eq!(write_handle.write(WRITE_BYTES).unwrap(), WRITE_BYTES.len(),);
        assert_eq!(output.lock().as_slice(), b"This is a \nmultiline output \n");
        write_handle.flush().unwrap();
        assert_eq!(
            output.lock().as_slice(),
            b"This is a \nmultiline output \nwithout newline termination\n"
        );
        output.lock().clear();

        const TERMINATED_BYTES: &[u8] = b"This is \nnewline terminated \noutput\n";
        assert_eq!(write_handle.write(TERMINATED_BYTES).unwrap(), TERMINATED_BYTES.len(),);
        assert_eq!(output.lock().as_slice(), b"This is \nnewline terminated \noutput\n");
    }

    #[fuchsia::test]
    fn single_handle_exceed_buffer_in_single_write() {
        const CAPACITY: usize = ShellWriterHandle::<Vec<u8>>::BUFFER_CAPACITY;
        // each case consists of a sequence of pairs, where each pair is a string to write, and
        // the expected output after writing the string.
        let cases = vec![
            (
                "exceed in one write",
                vec![("a".repeat(CAPACITY + 1), format!("{}\n", "a".repeat(CAPACITY + 1)))],
            ),
            (
                "exceed on second write",
                vec![
                    ("a".to_string(), "".to_string()),
                    ("a".repeat(CAPACITY + 1), format!("{}\n", "a".repeat(CAPACITY + 2))),
                ],
            ),
            (
                "exceed in one write, with newline",
                vec![(
                    format!("\n{}", "a".repeat(CAPACITY + 1)),
                    format!("\n{}\n", "a".repeat(CAPACITY + 1)),
                )],
            ),
            (
                "exceed in two writes, with newline",
                vec![
                    ("a".to_string(), "".to_string()),
                    (
                        format!("\n{}", "a".repeat(CAPACITY + 1)),
                        format!("a\n{}\n", "a".repeat(CAPACITY + 1)),
                    ),
                ],
            ),
        ];

        for (case_name, writes) in cases.into_iter() {
            let (handle_inner, output) = create_writer_inner_and_view();
            let mut write_handle = ShellWriterHandle::new_handle(handle_inner, None);
            for (write_no, (to_write, expected)) in writes.into_iter().enumerate() {
                assert_eq!(
                    write_handle.write(to_write.as_bytes()).unwrap(),
                    to_write.as_bytes().len(),
                    "Got wrong number of bytes written for write {:?} in case {}",
                    write_no,
                    case_name
                );
                assert_eq!(
                    String::from_utf8(output.lock().clone()).unwrap(),
                    expected,
                    "Buffer contains unexpected contents after write {:?} in case {}",
                    write_no,
                    case_name
                )
            }
        }
    }

    #[fuchsia::test]
    fn single_handle_with_prefix_multiple_line() {
        let (handle_inner, output) = create_writer_inner_and_view();
        let mut write_handle =
            ShellWriterHandle::new_handle(handle_inner, Some("[prefix] ".to_string()));
        const WRITE_BYTES: &[u8] = b"This is a \nmultiline output \nwithout newline termination";
        assert_eq!(write_handle.write(WRITE_BYTES).unwrap(), WRITE_BYTES.len(),);
        // Note we 'chunk' output in each write to avoid spamming the prefix, so the second
        // line won't contain the prefix.
        assert_eq!(output.lock().as_slice(), b"[prefix] This is a \nmultiline output \n");
        write_handle.flush().unwrap();
        assert_eq!(
            output.lock().as_slice(),
            "[prefix] This is a \nmultiline output \nwithout newline termination\n".as_bytes()
        );

        const TERMINATED_BYTES: &[u8] = b"This is \nnewline terminated \noutput\n";
        assert_eq!(write_handle.write(TERMINATED_BYTES).unwrap(), TERMINATED_BYTES.len(),);
        assert_eq!(
            output.lock().as_slice(),
            b"[prefix] This is a \nmultiline output \n\
            without newline termination\nThis is \nnewline terminated \noutput\n"
        );
    }

    #[fuchsia::test]
    fn multiple_handles() {
        let (handle_inner, output) = create_writer_inner_and_view();
        let mut handle_1 =
            ShellWriterHandle::new_handle(handle_inner.clone(), Some("[1] ".to_string()));
        let mut handle_2 = ShellWriterHandle::new_handle(handle_inner, Some("[2] ".to_string()));

        write!(handle_1, "hi from 1").unwrap();
        write!(handle_2, "hi from 2").unwrap();
        assert!(output.lock().is_empty());
        write!(handle_1, "\n").unwrap();
        assert_eq!(output.lock().as_slice(), "[1] hi from 1\n".as_bytes());
        write!(handle_2, "\n").unwrap();
        assert_eq!(output.lock().as_slice(), "[1] hi from 1\n[2] hi from 2\n".as_bytes());
    }

    // The following tests verify behavior of the shell writer when the inner writer
    // exhibits some allowed edge cases.

    #[fuchsia::test]
    fn output_written_when_inner_writer_writes_partial_buffer() {
        /// A writer that writes at most 3 bytes at a time.
        struct PartialOutputWriter(Vec<u8>);
        impl Write for PartialOutputWriter {
            fn write(&mut self, buf: &[u8]) -> Result<usize, Error> {
                if buf.len() >= 3 {
                    self.0.write(&buf[..3])
                } else {
                    self.0.write(buf)
                }
            }

            fn flush(&mut self) -> Result<(), Error> {
                self.0.flush()
            }
        }

        let inner = Arc::new(Mutex::new(ShellWriterHandleInner::new(PartialOutputWriter(vec![]))));
        let output = ShellWriterView(inner.clone());
        let mut write_handle =
            ShellWriterHandle::new_handle(inner.clone(), Some("[prefix] ".to_string()));
        assert_eq!(write_handle.write(b"hello").unwrap(), b"hello".len());
        assert!(output.lock().0.is_empty());
        assert_eq!(write_handle.write(b"\n").unwrap(), b"\n".len());
        assert_eq!(output.lock().0.as_slice(), b"[prefix] hello\n");

        let mut write_handle_2 =
            ShellWriterHandle::new_handle(inner, Some("[prefix2] ".to_string()));

        assert_eq!(write_handle.write(b"hello").unwrap(), b"hello".len());
        assert_eq!(write_handle_2.write(b"world").unwrap(), b"world".len());
        assert_eq!(output.lock().0.as_slice(), b"[prefix] hello\n");
        assert_eq!(write_handle.write(b"\n").unwrap(), b"\n".len());
        assert_eq!(output.lock().0.as_slice(), b"[prefix] hello\nhello\n");
        assert_eq!(write_handle_2.write(b"\n").unwrap(), b"\n".len());
        assert_eq!(output.lock().0.as_slice(), b"[prefix] hello\nhello\n[prefix2] world\n");
    }

    #[fuchsia::test]
    fn output_written_when_inner_writer_returns_interrupted() {
        /// A writer that returns interrupted on the first write attempt
        struct InterruptWriter {
            buf: Vec<u8>,
            returned_interrupt: bool,
        }
        impl Write for InterruptWriter {
            fn write(&mut self, buf: &[u8]) -> Result<usize, Error> {
                if !self.returned_interrupt {
                    self.returned_interrupt = true;
                    Err(ErrorKind::Interrupted.into())
                } else {
                    self.buf.write(buf)
                }
            }

            fn flush(&mut self) -> Result<(), Error> {
                self.buf.flush()
            }
        }

        let inner = Arc::new(Mutex::new(ShellWriterHandleInner::new(InterruptWriter {
            buf: vec![],
            returned_interrupt: false,
        })));
        let output = ShellWriterView(inner.clone());
        let mut write_handle =
            ShellWriterHandle::new_handle(inner.clone(), Some("[prefix] ".to_string()));
        assert_eq!(write_handle.write(b"hello").unwrap(), b"hello".len());
        assert!(output.lock().buf.is_empty());
        assert_eq!(write_handle.write(b"\n").unwrap(), b"\n".len());
        assert_eq!(output.lock().buf.as_slice(), b"[prefix] hello\n");

        let mut write_handle_2 =
            ShellWriterHandle::new_handle(inner.clone(), Some("[prefix2] ".to_string()));

        assert_eq!(write_handle.write(b"hello").unwrap(), b"hello".len());
        assert_eq!(write_handle_2.write(b"world").unwrap(), b"world".len());
        assert_eq!(output.lock().buf.as_slice(), b"[prefix] hello\n");
        assert_eq!(write_handle.write(b"\n").unwrap(), b"\n".len());
        assert_eq!(output.lock().buf.as_slice(), b"[prefix] hello\nhello\n");
        assert_eq!(write_handle_2.write(b"\n").unwrap(), b"\n".len());
        assert_eq!(output.lock().buf.as_slice(), b"[prefix] hello\nhello\n[prefix2] world\n");
    }

    #[fuchsia::test]
    async fn report_case_events() {
        let (shell_reporter, output) = ShellReporter::new_expose_writer_for_test();
        let run_reporter = RunReporter::new_for_test(shell_reporter);
        let suite_reporter =
            run_reporter.new_suite("test-suite", &SuiteId(0)).await.expect("create suite");
        suite_reporter.started(Timestamp::Unknown).await.expect("case started");
        let mut expected = "Running test 'test-suite'\n".to_string();
        assert_eq!(String::from_utf8(output.lock().clone()).unwrap(), expected,);

        let case_1_reporter =
            suite_reporter.new_case("case-1", &CaseId(0)).await.expect("create case");
        let case_2_reporter =
            suite_reporter.new_case("case-2", &CaseId(1)).await.expect("create case");

        case_1_reporter.started(Timestamp::Unknown).await.expect("case started");
        expected.push_str("[RUNNING]\tcase-1\n");
        assert_eq!(String::from_utf8(output.lock().clone()).unwrap(), expected,);

        case_2_reporter.started(Timestamp::Unknown).await.expect("case started");
        expected.push_str("[RUNNING]\tcase-2\n");
        assert_eq!(String::from_utf8(output.lock().clone()).unwrap(), expected,);

        case_1_reporter
            .stopped(&ReportedOutcome::Passed, Timestamp::Unknown)
            .await
            .expect("stop case");
        expected.push_str("[PASSED]\tcase-1\n");
        assert_eq!(String::from_utf8(output.lock().clone()).unwrap(), expected,);

        case_2_reporter
            .stopped(&ReportedOutcome::Failed, Timestamp::Unknown)
            .await
            .expect("stop case");
        expected.push_str("[FAILED]\tcase-2\n");
        assert_eq!(String::from_utf8(output.lock().clone()).unwrap(), expected,);

        case_1_reporter.finished().await.expect("finish case");
        case_2_reporter.finished().await.expect("finish case");
        suite_reporter
            .stopped(&ReportedOutcome::Failed, Timestamp::Unknown)
            .await
            .expect("stop suite");
        suite_reporter.finished().await.expect("finish suite");

        expected.push_str("\n");
        expected.push_str("Failed tests: case-2\n");
        expected.push_str("1 out of 2 tests passed...\n");
        expected.push_str("test-suite completed with result: FAILED\n");
        assert_eq!(String::from_utf8(output.lock().clone()).unwrap(), expected,);
    }

    #[fuchsia::test]
    async fn syslog_artifacts() {
        let (shell_reporter, output) = ShellReporter::new_expose_writer_for_test();
        let run_reporter = RunReporter::new_for_test(shell_reporter);
        let suite_reporter =
            run_reporter.new_suite("test-suite", &SuiteId(0)).await.expect("create suite");
        suite_reporter.started(Timestamp::Unknown).await.expect("case started");
        let mut syslog_writer =
            suite_reporter.new_artifact(&ArtifactType::Syslog).await.expect("create syslog");

        writeln!(syslog_writer, "[log] test syslog").expect("write to syslog");
        let mut expected = "Running test 'test-suite'\n".to_string();
        expected.push_str("[log] test syslog\n");
        assert_eq!(String::from_utf8(output.lock().clone()).unwrap(), expected);

        suite_reporter
            .stopped(&ReportedOutcome::Passed, Timestamp::Unknown)
            .await
            .expect("stop suite");
        writeln!(syslog_writer, "[log] more test syslog").expect("write to syslog");
        expected.push_str("[log] more test syslog\n");
        assert_eq!(String::from_utf8(output.lock().clone()).unwrap(), expected);

        suite_reporter.finished().await.expect("finish suite");
        expected.push_str("\n");
        expected.push_str("0 out of 0 tests passed...\n");
        expected.push_str("test-suite completed with result: PASSED\n");
        assert_eq!(String::from_utf8(output.lock().clone()).unwrap(), expected,);
    }

    #[fuchsia::test]
    async fn report_retricted_logs() {
        let (shell_reporter, output) = ShellReporter::new_expose_writer_for_test();
        let run_reporter = RunReporter::new_for_test(shell_reporter);
        let suite_reporter =
            run_reporter.new_suite("test-suite", &SuiteId(0)).await.expect("create suite");
        suite_reporter.started(Timestamp::Unknown).await.expect("case started");

        let case_reporter =
            suite_reporter.new_case("case-0", &CaseId(0)).await.expect("create case");
        case_reporter.started(Timestamp::Unknown).await.expect("case started");
        case_reporter
            .stopped(&ReportedOutcome::Passed, Timestamp::Unknown)
            .await
            .expect("stop case");
        case_reporter.finished().await.expect("finish case");

        let mut expected = "Running test 'test-suite'\n".to_string();
        expected.push_str("[RUNNING]\tcase-0\n");
        expected.push_str("[PASSED]\tcase-0\n");
        assert_eq!(String::from_utf8(output.lock().clone()).unwrap(), expected,);

        suite_reporter
            .stopped(&ReportedOutcome::Failed, Timestamp::Unknown)
            .await
            .expect("stop suite");
        let mut restricted_log = suite_reporter
            .new_artifact(&ArtifactType::RestrictedLog)
            .await
            .expect("create restricted log");
        write!(restricted_log, "suite restricted log").expect("write to restricted log");
        drop(restricted_log);

        suite_reporter.finished().await.expect("finish suite");
        expected.push_str("\n");
        expected.push_str("1 out of 1 tests passed...\n");
        expected.push_str("\nTest test-suite produced unexpected high-severity logs:\n");
        expected.push_str("----------------xxxxx----------------\n");
        expected.push_str("suite restricted log\n\n");
        expected.push_str("----------------xxxxx----------------\n");
        expected.push_str("Failing this test. See: https://fuchsia.dev/fuchsia-src/concepts/testing/logs#restricting_log_severity\n");
        expected.push_str("\ntest-suite completed with result: FAILED\n");
        assert_eq!(String::from_utf8(output.lock().clone()).unwrap(), expected,);
    }

    #[fuchsia::test]
    async fn stdout_artifacts() {
        let (shell_reporter, output) = ShellReporter::new_expose_writer_for_test();
        let run_reporter = RunReporter::new_for_test(shell_reporter);
        let suite_reporter =
            run_reporter.new_suite("test-suite", &SuiteId(0)).await.expect("create suite");
        suite_reporter.started(Timestamp::Unknown).await.expect("case started");

        let case_0_reporter =
            suite_reporter.new_case("case-0", &CaseId(0)).await.expect("create case");
        let case_1_reporter =
            suite_reporter.new_case("case-1", &CaseId(1)).await.expect("create case");
        case_0_reporter.started(Timestamp::Unknown).await.expect("start case");
        case_1_reporter.started(Timestamp::Unknown).await.expect("start case");
        let mut expected = "Running test 'test-suite'\n".to_string();
        expected.push_str("[RUNNING]\tcase-0\n[RUNNING]\tcase-1\n");
        assert_eq!(String::from_utf8(output.lock().clone()).unwrap(), expected,);

        let mut case_0_stdout =
            case_0_reporter.new_artifact(&ArtifactType::Stdout).await.expect("create artifact");
        let mut case_1_stdout =
            case_1_reporter.new_artifact(&ArtifactType::Stdout).await.expect("create artifact");

        writeln!(case_0_stdout, "stdout from case 0").expect("write to stdout");
        writeln!(case_1_stdout, "stdout from case 1").expect("write to stdout");

        expected.push_str("[stdout - case-0]\nstdout from case 0\n");
        expected.push_str("[stdout - case-1]\nstdout from case 1\n");
        assert_eq!(String::from_utf8(output.lock().clone()).unwrap(), expected,);

        case_0_reporter
            .stopped(&ReportedOutcome::Passed, Timestamp::Unknown)
            .await
            .expect("stop case");
        case_1_reporter
            .stopped(&ReportedOutcome::Passed, Timestamp::Unknown)
            .await
            .expect("stop case");
        expected.push_str("[PASSED]\tcase-0\n");
        expected.push_str("[PASSED]\tcase-1\n");
        assert_eq!(String::from_utf8(output.lock().clone()).unwrap(), expected,);

        case_0_reporter.finished().await.expect("finish case");
        case_1_reporter.finished().await.expect("finish case");
        suite_reporter
            .stopped(&ReportedOutcome::Passed, Timestamp::Unknown)
            .await
            .expect("stop suite");

        suite_reporter.finished().await.expect("finish suite");
        expected.push_str("\n");
        expected.push_str("2 out of 2 tests passed...\n");
        expected.push_str("test-suite completed with result: PASSED\n");
        assert_eq!(String::from_utf8(output.lock().clone()).unwrap(), expected,);
    }

    #[fuchsia::test]
    async fn report_unfinished() {
        let (shell_reporter, output) = ShellReporter::new_expose_writer_for_test();
        let run_reporter = RunReporter::new_for_test(shell_reporter);
        let suite_reporter =
            run_reporter.new_suite("test-suite", &SuiteId(0)).await.expect("create suite");
        suite_reporter.started(Timestamp::Unknown).await.expect("suite started");

        let case_reporter =
            suite_reporter.new_case("case-0", &CaseId(0)).await.expect("create case");
        case_reporter.started(Timestamp::Unknown).await.expect("case started");
        case_reporter
            .stopped(&ReportedOutcome::Passed, Timestamp::Unknown)
            .await
            .expect("stop case");
        case_reporter.finished().await.expect("finish case");
        let mut expected = "Running test 'test-suite'\n".to_string();
        expected.push_str("[RUNNING]\tcase-0\n[PASSED]\tcase-0\n");
        assert_eq!(String::from_utf8(output.lock().clone()).unwrap(), expected,);

        // add a case that finishes with ERROR
        let unfinished_case_1 =
            suite_reporter.new_case("case-1", &CaseId(1)).await.expect("create case");
        unfinished_case_1.started(Timestamp::Unknown).await.expect("case started");
        unfinished_case_1
            .stopped(&ReportedOutcome::Error, Timestamp::Unknown)
            .await
            .expect("stop case");
        unfinished_case_1.finished().await.expect("finish case");
        expected.push_str("[RUNNING]\tcase-1\n");
        assert_eq!(String::from_utf8(output.lock().clone()).unwrap(), expected,);

        // add a case that doesn't finish at all
        let unfinished_case_2 =
            suite_reporter.new_case("case-2", &CaseId(2)).await.expect("create case");
        unfinished_case_2.started(Timestamp::Unknown).await.expect("case started");
        unfinished_case_2.finished().await.expect("finish case");
        expected.push_str("[RUNNING]\tcase-2\n");
        assert_eq!(String::from_utf8(output.lock().clone()).unwrap(), expected,);

        suite_reporter
            .stopped(&ReportedOutcome::Failed, Timestamp::Unknown)
            .await
            .expect("stop suite");
        suite_reporter.finished().await.expect("finish suite");
        expected.push_str("\n");
        expected.push_str("\nThe following test(s) never completed:\n");
        expected.push_str("case-1\n");
        expected.push_str("case-2\n");
        expected.push_str("1 out of 3 tests passed...\n");
        expected.push_str("test-suite completed with result: FAILED\n");
        assert_eq!(String::from_utf8(output.lock().clone()).unwrap(), expected,);
    }

    #[fuchsia::test]
    async fn report_cancelled_suite() {
        let (shell_reporter, output) = ShellReporter::new_expose_writer_for_test();
        let run_reporter = RunReporter::new_for_test(shell_reporter);
        let suite_reporter =
            run_reporter.new_suite("test-suite", &SuiteId(0)).await.expect("create suite");
        suite_reporter.started(Timestamp::Unknown).await.expect("suite started");

        let case_reporter =
            suite_reporter.new_case("case", &CaseId(0)).await.expect("create new case");
        case_reporter.started(Timestamp::Unknown).await.expect("case started");
        case_reporter.finished().await.expect("case finished");
        suite_reporter
            .stopped(&ReportedOutcome::Cancelled, Timestamp::Unknown)
            .await
            .expect("stop suite");
        suite_reporter.finished().await.expect("case finished");

        let mut expected = "Running test 'test-suite'\n".to_string();
        expected.push_str("[RUNNING]\tcase\n");
        expected.push_str("\n");
        expected.push_str("\nThe following test(s) never completed:\n");
        expected.push_str("case\n");
        expected.push_str("0 out of 1 tests passed...\n");
        expected.push_str("test-suite was cancelled before completion.\n");
        assert_eq!(String::from_utf8(output.lock().clone()).unwrap(), expected,);
    }

    #[fuchsia::test]
    async fn report_suite_did_not_finish() {
        let (shell_reporter, output) = ShellReporter::new_expose_writer_for_test();
        let run_reporter = RunReporter::new_for_test(shell_reporter);
        let suite_reporter =
            run_reporter.new_suite("test-suite", &SuiteId(0)).await.expect("create suite");
        suite_reporter.started(Timestamp::Unknown).await.expect("suite started");

        let case_reporter =
            suite_reporter.new_case("case", &CaseId(0)).await.expect("create new case");
        case_reporter.started(Timestamp::Unknown).await.expect("case started");
        case_reporter.finished().await.expect("case finished");
        suite_reporter
            .stopped(&ReportedOutcome::DidNotFinish, Timestamp::Unknown)
            .await
            .expect("stop suite");
        suite_reporter.finished().await.expect("case finished");

        let mut expected = "Running test 'test-suite'\n".to_string();
        expected.push_str("[RUNNING]\tcase\n");
        expected.push_str("\n");
        expected.push_str("\nThe following test(s) never completed:\n");
        expected.push_str("case\n");
        expected.push_str("0 out of 1 tests passed...\n");
        expected.push_str("test-suite did not complete successfully.\n");
        assert_eq!(String::from_utf8(output.lock().clone()).unwrap(), expected,);
    }
}
