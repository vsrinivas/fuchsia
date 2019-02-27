use byteorder::{BigEndian, ReadBytesExt};
use std::io::{Read, Seek, SeekFrom};
use std::mem::size_of;
use std::string::{FromUtf8Error, FromUtf16Error};

use {Date, Error, Result, PlistEvent, u64_to_usize};

impl From<FromUtf8Error> for Error {
    fn from(_: FromUtf8Error) -> Error {
        Error::InvalidData
    }
}

impl From<FromUtf16Error> for Error {
    fn from(_: FromUtf16Error) -> Error {
        Error::InvalidData
    }
}

struct StackItem {
    object_refs: Vec<u64>,
    ty: StackType,
}

enum StackType {
    Array,
    Dict,
    Root,
}

/// https://opensource.apple.com/source/CF/CF-550/CFBinaryPList.c
/// https://hg.python.org/cpython/file/3.4/Lib/plistlib.py
pub struct EventReader<R> {
    stack: Vec<StackItem>,
    object_offsets: Vec<u64>,
    reader: R,
    ref_size: u8,
    finished: bool,
    // The largest single allocation allowed for this Plist.
    // Equal to the number of bytes in the Plist minus the magic and trailer.
    max_allocation_bytes: usize,
    // The maximum number of nested arrays and dicts allowed in the plist.
    max_stack_depth: usize,
    // The maximum number of objects that can be created. Default 10 * object_offsets.len().
    // Binary plists can contain circular references.
    max_objects: usize,
    // The number of objects created so far.
    current_objects: usize,
}

impl<R: Read + Seek> EventReader<R> {
    pub fn new(reader: R) -> EventReader<R> {
        EventReader {
            stack: Vec::new(),
            object_offsets: Vec::new(),
            reader: reader,
            ref_size: 0,
            finished: false,
            max_allocation_bytes: 0,
            max_stack_depth: 200,
            max_objects: 0,
            current_objects: 0,
        }
    }

    fn can_allocate(&self, len: u64, size: usize) -> bool {
        let byte_len = len.saturating_mul(size as u64);
        byte_len <= self.max_allocation_bytes as u64
    }

    fn allocate_vec<T>(&self, len: u64, size: usize) -> Result<Vec<T>> {
        if self.can_allocate(len, size) {
            Ok(Vec::with_capacity(len as usize))
        } else {
            Err(Error::InvalidData)
        }
    }

    fn read_trailer(&mut self) -> Result<()> {
        self.reader.seek(SeekFrom::Start(0))?;
        let mut magic = [0; 8];
        self.reader.read_exact(&mut magic)?;
        if &magic != b"bplist00" {
            return Err(Error::InvalidData);
        }

        // Trailer starts with 6 bytes of padding
        let trailer_start = self.reader.seek(SeekFrom::End(-32 + 6))?;

        let offset_size = self.reader.read_u8()?;
        match offset_size {
            1 | 2 | 4 | 8 => (),
            _ => return Err(Error::InvalidData)
        }

        self.ref_size = self.reader.read_u8()?;
        match self.ref_size {
            1 | 2 | 4 | 8 => (),
            _ => return Err(Error::InvalidData)
        }

        let num_objects = self.reader.read_u64::<BigEndian>()?;
        let top_object = self.reader.read_u64::<BigEndian>()?;
        let offset_table_offset = self.reader.read_u64::<BigEndian>()?;

        // File size minus trailer and header
        // Truncated to max(usize)
        self.max_allocation_bytes = trailer_start.saturating_sub(8) as usize;

        // Read offset table
        self.reader.seek(SeekFrom::Start(offset_table_offset))?;
        self.object_offsets = self.read_ints(num_objects, offset_size)?;

        self.max_objects = self.object_offsets.len() * 10;

        // Seek to top object
        self.stack.push(StackItem {
            object_refs: vec![top_object],
            ty: StackType::Root,
        });

        Ok(())
    }

    fn read_ints(&mut self, len: u64, size: u8) -> Result<Vec<u64>> {
        let mut ints = self.allocate_vec(len, size as usize)?;
        for _ in 0..len {
            match size {
                1 => ints.push(self.reader.read_u8()? as u64),
                2 => ints.push(self.reader.read_u16::<BigEndian>()? as u64),
                4 => ints.push(self.reader.read_u32::<BigEndian>()? as u64),
                8 => ints.push(self.reader.read_u64::<BigEndian>()? as u64),
                _ => return Err(Error::InvalidData),
            }
        }
        Ok(ints)
    }

    fn read_refs(&mut self, len: u64) -> Result<Vec<u64>> {
        let ref_size = self.ref_size;
        self.read_ints(len, ref_size)
    }

    fn read_object_len(&mut self, len: u8) -> Result<u64> {
        if (len & 0x0f) == 0x0f {
            let len_power_of_two = self.reader.read_u8()? & 0x03;
            Ok(match len_power_of_two {
                0 => self.reader.read_u8()? as u64,
                1 => self.reader.read_u16::<BigEndian>()? as u64,
                2 => self.reader.read_u32::<BigEndian>()? as u64,
                3 => self.reader.read_u64::<BigEndian>()?,
                _ => return Err(Error::InvalidData),
            })
        } else {
            Ok(len as u64)
        }
    }

    fn read_data(&mut self, len: u64) -> Result<Vec<u8>> {
        let mut data = self.allocate_vec(len, size_of::<u8>())?;
        // Safe as u8 is a Copy type and we have already know len has been allocated.
        unsafe { data.set_len(len as usize) }
        self.reader.read_exact(&mut data)?;
        Ok(data)
    }

    fn seek_to_object(&mut self, object_ref: u64) -> Result<u64> {
        let object_ref = u64_to_usize(object_ref)?;
        let offset = *self.object_offsets.get(object_ref).ok_or(Error::InvalidData)?;
        Ok(self.reader.seek(SeekFrom::Start(offset))?)
    }

    fn read_next(&mut self) -> Result<Option<PlistEvent>> {
        if self.ref_size == 0 {
            // Initialise here rather than in new
            self.read_trailer()?;
        }

        let object_ref = match self.stack.last_mut() {
            Some(stack_item) => stack_item.object_refs.pop(),
            // Reached the end of the plist
            None => return Ok(None),
        };

        match object_ref {
            Some(object_ref) => {
                if self.current_objects > self.max_objects {
                    return Err(Error::InvalidData);
                }
                self.current_objects += 1;
                self.seek_to_object(object_ref)?;
            }
            None => {
                // We're at the end of an array or dict. Pop the top stack item and return
                let item = self.stack.pop().unwrap();
                match item.ty {
                    StackType::Array => return Ok(Some(PlistEvent::EndArray)),
                    StackType::Dict => return Ok(Some(PlistEvent::EndDictionary)),
                    // We're at the end of the plist
                    StackType::Root => return Ok(None),
                }
            }
        }

        let token = self.reader.read_u8()?;
        let ty = (token & 0xf0) >> 4;
        let size = token & 0x0f;

        let result = match (ty, size) {
            (0x0, 0x00) => return Err(Error::InvalidData), // null
            (0x0, 0x08) => Some(PlistEvent::BooleanValue(false)),
            (0x0, 0x09) => Some(PlistEvent::BooleanValue(true)),
            (0x0, 0x0f) => return Err(Error::InvalidData), // fill
            (0x1, 0) => Some(PlistEvent::IntegerValue(self.reader.read_u8()? as i64)),
            (0x1, 1) => Some(PlistEvent::IntegerValue(self.reader.read_u16::<BigEndian>()? as i64)),
            (0x1, 2) => Some(PlistEvent::IntegerValue(self.reader.read_u32::<BigEndian>()? as i64)),
            (0x1, 3) => Some(PlistEvent::IntegerValue(self.reader.read_i64::<BigEndian>()?)),
            (0x1, 4) => return Err(Error::InvalidData), // 128 bit int
            (0x1, _) => return Err(Error::InvalidData), // variable length int
            (0x2, 2) => Some(PlistEvent::RealValue(self.reader.read_f32::<BigEndian>()? as f64)),
            (0x2, 3) => Some(PlistEvent::RealValue(self.reader.read_f64::<BigEndian>()?)),
            (0x2, _) => return Err(Error::InvalidData), // odd length float
            (0x3, 3) => {
                // Date. Seconds since 1/1/2001 00:00:00.
                let secs = self.reader.read_f64::<BigEndian>()?;
                Some(PlistEvent::DateValue(Date::from_seconds_since_plist_epoch(secs)?))
            }
            (0x4, n) => {
                // Data
                let len = self.read_object_len(n)?;
                Some(PlistEvent::DataValue(self.read_data(len)?))
            }
            (0x5, n) => {
                // ASCII string
                let len = self.read_object_len(n)?;
                let raw = self.read_data(len)?;
                let string = String::from_utf8(raw)?;
                Some(PlistEvent::StringValue(string))
            }
            (0x6, n) => {
                // UTF-16 string
                let len_utf16_codepoints = self.read_object_len(n)?;
                let mut raw_utf16 = self.allocate_vec(len_utf16_codepoints, size_of::<u16>())?;

                for _ in 0..len_utf16_codepoints {
                    raw_utf16.push(self.reader.read_u16::<BigEndian>()?);
                }

                let string = String::from_utf16(&raw_utf16)?;
                Some(PlistEvent::StringValue(string))
            }
            (0xa, n) => {
                // Array
                let len = self.read_object_len(n)?;
                let mut object_refs = self.read_refs(len)?;
                // Reverse so we can pop off the end of the stack in order
                object_refs.reverse();

                self.stack.push(StackItem {
                    ty: StackType::Array,
                    object_refs: object_refs,
                });

                Some(PlistEvent::StartArray(Some(len)))
            }
            (0xd, n) => {
                // Dict
                let len = self.read_object_len(n)?;
                let key_refs = self.read_refs(len)?;
                let value_refs = self.read_refs(len)?;

                let mut object_refs = self.allocate_vec(len * 2, self.ref_size as usize)?;
                let len = key_refs.len();
                for i in 1..len + 1 {
                    // Reverse so we can pop off the end of the stack in order
                    object_refs.push(value_refs[len - i]);
                    object_refs.push(key_refs[len - i]);
                }

                self.stack.push(StackItem {
                    ty: StackType::Dict,
                    object_refs: object_refs,
                });

                Some(PlistEvent::StartDictionary(Some(len as u64)))
            }
            (_, _) => return Err(Error::InvalidData),
        };

        // Prevent stack overflows when recursively parsing plist.
        if self.stack.len() > self.max_stack_depth {
            return Err(Error::InvalidData);
        }

        Ok(result)
    }
}

impl<R: Read + Seek> Iterator for EventReader<R> {
    type Item = Result<PlistEvent>;

    fn next(&mut self) -> Option<Result<PlistEvent>> {
        if self.finished {
            None
        } else {
            match self.read_next() {
                Ok(Some(event)) => Some(Ok(event)),
                Err(err) => {
                    self.finished = true;
                    Some(Err(err))
                }
                Ok(None) => {
                    self.finished = true;
                    None
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use chrono::{TimeZone, Utc};
    use std::fs::File;
    use std::path::Path;

    use super::*;
    use PlistEvent;

    #[test]
    fn streaming_parser() {
        use PlistEvent::*;

        let reader = File::open(&Path::new("./tests/data/binary.plist")).unwrap();
        let streaming_parser = EventReader::new(reader);
        let events: Vec<PlistEvent> = streaming_parser.map(|e| e.unwrap()).collect();

        let comparison = &[StartDictionary(Some(6)),
                           StringValue("Lines".to_owned()),
                           StartArray(Some(2)),
                           StringValue("It is a tale told by an idiot,".to_owned()),
                           StringValue("Full of sound and fury, signifying nothing.".to_owned()),
                           EndArray,
                           StringValue("Death".to_owned()),
                           IntegerValue(1564),
                           StringValue("Height".to_owned()),
                           RealValue(1.60),
                           StringValue("Birthdate".to_owned()),
                           DateValue(Utc.ymd(1981, 05, 16).and_hms(11, 32, 06).into()),
                           StringValue("Author".to_owned()),
                           StringValue("William Shakespeare".to_owned()),
                           StringValue("Data".to_owned()),
                           DataValue(vec![0, 0, 0, 190, 0, 0, 0, 3, 0, 0, 0, 30, 0, 0, 0]),
                           EndDictionary];

        assert_eq!(events, comparison);
    }

    #[test]
    fn utf16_plist() {
        use PlistEvent::*;

        let reader = File::open(&Path::new("./tests/data/utf16_bplist.plist")).unwrap();
        let streaming_parser = EventReader::new(reader);
        let mut events: Vec<PlistEvent> = streaming_parser.map(|e| e.unwrap()).collect();

        assert_eq!(events[2], StringValue("\u{2605} or better".to_owned()));

        let poem = if let StringValue(ref mut poem) = events[4] {
            poem
        } else {
            panic!("not a string")
        };
        assert_eq!(poem.len(), 643);
        assert_eq!(poem.pop().unwrap(), '\u{2605}');
    }
}
