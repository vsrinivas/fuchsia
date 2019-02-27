mod de;
mod ser;

pub use self::de::Deserializer;
pub use self::ser::Serializer;

use serde_base::de::{Deserialize, DeserializeOwned};
use serde_base::ser::Serialize;
use std::io::{Read, Seek, Write};

use Result;
use EventReader;
use xml;

pub fn deserialize<R: Read + Seek, T: DeserializeOwned>(reader: R) -> Result<T> {
    let reader = EventReader::new(reader);
    let mut de = Deserializer::new(reader);
    Deserialize::deserialize(&mut de)
}

pub fn serialize_to_xml<W: Write, T: Serialize>(writer: W, value: &T) -> Result<()> {
    let writer = xml::EventWriter::new(writer);
    let mut ser = Serializer::new(writer);
    value.serialize(&mut ser)
}
