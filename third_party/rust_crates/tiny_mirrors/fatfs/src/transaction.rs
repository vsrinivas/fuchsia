use std::{
    io::{Error, ErrorKind, Read, Result, Seek, SeekFrom, Write},
    vec::Vec,
};

struct Op {
    offset: u64,
    data: Vec<u8>,
}

impl Op {
    fn end(&self) -> u64 {
        self.offset + self.data.len() as u64
    }
}

// TransactionManager provides the ability to queue up changes and then later commit or revert those
// changes. It simplifies the process of reverting all changes. It is not designed to work with very
// large numbers of changes.
pub struct TransactionManager<T> {
    inner: T,
    ops: Vec<Op>,
    active: bool,
}

impl<T: Seek + Write> TransactionManager<T> {
    pub fn new(inner: T) -> Self {
        TransactionManager { inner, ops: Vec::new(), active: false }
    }

    #[allow(dead_code)]
    pub fn into_inner(self) -> T {
        self.inner
    }

    #[must_use]
    pub fn begin_transaction(&mut self) -> bool {
        if self.active {
            false
        } else {
            self.active = true;
            true
        }
    }

    pub fn commit(&mut self) -> Result<()> {
        assert!(self.active);
        self.active = false;
        for op in self.ops.drain(..) {
            self.inner.seek(SeekFrom::Start(op.offset))?;
            let mut buf = op.data.as_slice();
            while !buf.is_empty() {
                let done = self.inner.write(buf)?;
                if done == 0 {
                    return Err(Error::new(ErrorKind::WriteZero, "Inner write failed"));
                }
                buf = &buf[done..];
            }
        }
        Ok(())
    }

    pub fn revert(&mut self) {
        assert!(self.active);
        self.active = false;
        self.ops.clear();
    }

    pub fn borrow_inner(&self) -> &T {
        &self.inner
    }
}

impl<T: Read + Seek> Read for TransactionManager<T> {
    fn read(&mut self, mut buf: &mut [u8]) -> Result<usize> {
        if !self.active {
            return self.inner.read(buf);
        }

        let mut offset = self.inner.seek(SeekFrom::Current(0))?;
        let mut i = 0;
        while i < self.ops.len() && self.ops[i].end() <= offset {
            i += 1;
        }
        let mut done = 0;
        while !buf.is_empty() {
            let mut to_do;
            let next_offset = if i < self.ops.len() { self.ops[i].offset } else { u64::MAX };
            if next_offset <= offset {
                // Copy from our copy.
                to_do = std::cmp::min(self.ops[i].end() - offset, buf.len() as u64) as usize;
                let data_offset = (offset - next_offset) as usize;
                buf[..to_do].copy_from_slice(&self.ops[i].data[data_offset..data_offset + to_do]);
                i += 1;
                self.inner.seek(SeekFrom::Current(to_do as i64))?;
            } else {
                // Fall through to inner.
                to_do = std::cmp::min(next_offset - offset, buf.len() as u64) as usize;
                to_do = self.inner.read(&mut buf[..to_do])?;
                if to_do == 0 {
                    return Ok(done);
                }
            }
            buf = &mut buf[to_do..];
            offset += to_do as u64;
            done += to_do;
        }
        return Ok(done);
    }
}

impl<T: Seek + Write> Write for TransactionManager<T> {
    fn write(&mut self, buf: &[u8]) -> Result<usize> {
        if !self.active {
            return self.inner.write(buf);
        }

        let offset = self.inner.seek(SeekFrom::Current(0))?;
        let mut i = 0;
        while i < self.ops.len() && self.ops[i].end() < offset {
            i += 1;
        }
        if i >= self.ops.len() {
            // End of the list, just append a new operation.
            self.ops.push(Op { offset, data: buf.to_vec() });
        } else if self.ops[i].end() == offset {
            // Extend an existing entry.
            self.ops[i].data.extend_from_slice(buf);
        } else if self.ops[i].offset < offset {
            // Existing entry overlaps.
            let data_offset = (offset - self.ops[i].offset) as usize;
            if self.ops[i].end() >= offset + buf.len() as u64 {
                // Existing entry already encompasses all of this write.
                self.ops[i].data[data_offset..data_offset + buf.len()].copy_from_slice(buf);
            } else {
                // Partial overlap + extension.
                let to_do = self.ops[i].data.len() - data_offset;
                self.ops[i].data[data_offset..].copy_from_slice(&buf[..to_do]);
                self.ops[i].data.extend_from_slice(&buf[to_do..]);
            }
        } else {
            // No overlap at the beginning.
            self.ops.insert(i, Op { offset: offset, data: buf.to_vec() });
        }
        // Trim any of the following extents as required.
        let end = self.ops[i].end();
        i += 1;
        while i < self.ops.len() && self.ops[i].offset < end {
            if self.ops[i].end() <= end {
                self.ops.remove(i);
            } else {
                // Delete the beginning of the entry.
                let to_delete = end - self.ops[i].offset;
                self.ops[i].offset += to_delete;
                self.ops[i].data.drain(..to_delete as usize);
                break;
            }
        }
        self.inner.seek(SeekFrom::Current(buf.len() as i64))?;
        Ok(buf.len())
    }

    fn flush(&mut self) -> Result<()> {
        self.inner.flush()
    }
}

impl<T: std::io::Seek> Seek for TransactionManager<T> {
    fn seek(&mut self, pos: SeekFrom) -> Result<u64> {
        self.inner.seek(pos)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::TransactionManager,
        std::io::{Cursor, Read, Seek, SeekFrom, Write},
    };

    #[test]
    fn test_read_fall_through_when_no_transaction() {
        let mut manager = TransactionManager::new(Cursor::new(vec![55; 100]));
        let mut read_buf = vec![0; 3];
        assert_eq!(manager.read(&mut read_buf).expect("read failed"), 3);
        assert_eq!(&read_buf, &[55, 55, 55]);
    }

    #[test]
    fn test_write_fall_through_when_no_transaction() {
        let mut manager = TransactionManager::new(Cursor::new(vec![0; 4]));
        let write_buf = vec![1, 2, 3];
        assert_eq!(manager.write(&write_buf).expect("write failed"), 3);
        assert_eq!(&manager.into_inner().into_inner(), &[1, 2, 3, 0]);
    }

    #[test]
    fn test_read_part_transaction_part_inner() {
        let mut manager = TransactionManager::new(Cursor::new(vec![55; 100]));
        assert!(manager.begin_transaction());
        assert_eq!(manager.write(&[1, 2, 3]).expect("write failed"), 3);
        assert_eq!(manager.seek(SeekFrom::Start(10)).expect("seek failed"), 10);
        assert_eq!(manager.write(&[4, 5, 6]).expect("write failed"), 3);
        assert_eq!(manager.seek(SeekFrom::Start(7)).expect("seek failed"), 7);
        let mut read_buf = vec![0; 7];
        assert_eq!(manager.read(&mut read_buf).expect("read failed"), 7);
        assert_eq!(&read_buf, &[55, 55, 55, 4, 5, 6, 55]);
        assert_eq!(manager.seek(SeekFrom::Current(0)).expect("seek failed"), 14);
    }

    #[test]
    fn test_write_extend_entry() {
        let mut manager = TransactionManager::new(Cursor::new(vec![55; 100]));
        assert!(manager.begin_transaction());
        assert_eq!(manager.write(&[1, 2, 3]).expect("write failed"), 3);
        assert_eq!(manager.seek(SeekFrom::Start(10)).expect("seek failed"), 10);
        assert_eq!(manager.write(&[4, 5, 6]).expect("write failed"), 3);
        assert_eq!(manager.write(&[7, 8, 9]).expect("write failed"), 3);
        assert_eq!(manager.seek(SeekFrom::Start(11)).expect("seek failed"), 11);
        let mut read_buf = vec![0; 4];
        assert_eq!(manager.read(&mut read_buf).expect("read failed"), 4);
        assert_eq!(&read_buf, &[5, 6, 7, 8]);
    }

    #[test]
    fn test_write_existing_entry_encompasses_write() {
        let mut manager = TransactionManager::new(Cursor::new(vec![55; 100]));
        assert!(manager.begin_transaction());
        assert_eq!(manager.write(&[1, 2, 3]).expect("write failed"), 3);
        assert_eq!(manager.seek(SeekFrom::Start(10)).expect("seek failed"), 10);
        assert_eq!(manager.write(&[4, 5, 6, 7, 8, 9]).expect("write failed"), 6);
        assert_eq!(manager.seek(SeekFrom::Start(12)).expect("seek failed"), 12);
        assert_eq!(manager.write(&[99, 100]).expect("write failed"), 2);
        assert_eq!(manager.seek(SeekFrom::Start(11)).expect("seek failed"), 11);
        let mut read_buf = vec![0; 4];
        assert_eq!(manager.read(&mut read_buf).expect("read failed"), 4);
        assert_eq!(&read_buf, &[5, 99, 100, 8]);
    }

    #[test]
    fn test_write_partial_overlap_and_extension() {
        let mut manager = TransactionManager::new(Cursor::new(vec![55; 100]));
        assert!(manager.begin_transaction());
        assert_eq!(manager.write(&[1, 2, 3]).expect("write failed"), 3);
        assert_eq!(manager.seek(SeekFrom::Start(10)).expect("seek failed"), 10);
        assert_eq!(manager.write(&[4, 5, 6, 7, 8, 9]).expect("write failed"), 6);
        assert_eq!(manager.seek(SeekFrom::Start(14)).expect("seek failed"), 14);
        assert_eq!(manager.write(&[99, 100, 101, 102]).expect("write failed"), 4);
        assert_eq!(manager.seek(SeekFrom::Start(13)).expect("seek failed"), 13);
        let mut read_buf = vec![0; 6];
        assert_eq!(manager.read(&mut read_buf).expect("read failed"), 6);
        assert_eq!(&read_buf, &[7, 99, 100, 101, 102, 55]);
    }

    #[test]
    fn test_write_no_overlap() {
        let mut manager = TransactionManager::new(Cursor::new(vec![55; 100]));
        assert!(manager.begin_transaction());
        assert_eq!(manager.write(&[1, 2, 3]).expect("write failed"), 3);
        assert_eq!(manager.seek(SeekFrom::Start(8)).expect("seek failed"), 8);
        assert_eq!(manager.write(&[4, 5, 6]).expect("write failed"), 3);
        assert_eq!(manager.seek(SeekFrom::Start(4)).expect("seek failed"), 4);
        assert_eq!(manager.write(&[7, 8, 9]).expect("write failed"), 3);
        assert_eq!(manager.seek(SeekFrom::Start(2)).expect("seek failed"), 2);
        let mut read_buf = vec![0; 10];
        assert_eq!(manager.read(&mut read_buf).expect("read failed"), 10);
        assert_eq!(&read_buf, &[3, 55, 7, 8, 9, 55, 4, 5, 6, 55]);
    }

    #[test]
    fn test_trim_following_entries() {
        let mut manager = TransactionManager::new(Cursor::new(vec![55; 100]));
        assert!(manager.begin_transaction());
        assert_eq!(manager.seek(SeekFrom::Start(2)).expect("seek failed"), 2);
        assert_eq!(manager.write(&[1, 2, 3]).expect("write failed"), 3);
        assert_eq!(manager.seek(SeekFrom::Current(1)).expect("seek failed"), 6);
        assert_eq!(manager.write(&[4, 5, 6]).expect("write failed"), 3);
        assert_eq!(manager.seek(SeekFrom::Start(1)).expect("seek failed"), 1);
        assert_eq!(manager.write(&[100, 101, 102, 103, 104, 105]).expect("write failed"), 6);
        assert_eq!(manager.seek(SeekFrom::Start(0)).expect("seek failed"), 0);
        let mut read_buf = vec![0; 10];
        assert_eq!(manager.read(&mut read_buf).expect("read failed"), 10);
        assert_eq!(&read_buf, &[55, 100, 101, 102, 103, 104, 105, 5, 6, 55]);
    }

    #[test]
    fn test_revert_transaction() {
        let mut manager = TransactionManager::new(Cursor::new(vec![1, 2, 3, 4, 5, 6]));
        assert!(manager.begin_transaction());
        assert_eq!(manager.write(&[7, 8, 9]).expect("write failed"), 3);
        manager.revert();
        assert_eq!(manager.seek(SeekFrom::Start(1)).expect("seek failed"), 1);
        let mut read_buf = vec![0; 4];
        assert_eq!(manager.read(&mut read_buf).expect("read failed"), 4);
        assert_eq!(&read_buf, &[2, 3, 4, 5]);
    }

    #[test]
    fn test_commit_transaction() {
        let mut manager = TransactionManager::new(Cursor::new(vec![55; 10]));
        assert!(manager.begin_transaction());
        assert_eq!(manager.write(&[1, 2]).expect("write failed"), 2);
        assert_eq!(manager.seek(SeekFrom::Current(1)).expect("seek failed"), 3);
        assert_eq!(manager.write(&[3, 4]).expect("write failed"), 2);
        assert_eq!(manager.seek(SeekFrom::Current(1)).expect("seek failed"), 6);
        assert_eq!(manager.write(&[5, 6]).expect("write failed"), 2);
        manager.commit().expect("commit failed");
        assert_eq!(&manager.into_inner().into_inner(), &[1, 2, 55, 3, 4, 55, 5, 6, 55, 55]);
    }
}
