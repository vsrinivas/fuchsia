// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file

use super::Redactor;
use serde::{ser, Serialize, Serializer};
use std::sync::Arc;

pub struct RedactedItem<M> {
    pub inner: Arc<M>,
    pub redactor: Arc<Redactor>,
}

impl<M> Serialize for RedactedItem<M>
where
    M: Serialize,
{
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        let serializer = RedactingSerializer { inner: serializer, redactor: &self.redactor };
        self.inner.serialize(serializer)
    }
}

/// Ensures that strings in the serialized output have been redacted. Works by forwarding to
/// [`RedactingSerializer`].
pub struct Redacted<'m, 'r, M: ?Sized> {
    pub inner: &'m M,
    pub redactor: &'r Redactor,
}

impl<'m, 'r, M> Serialize for Redacted<'m, 'r, M>
where
    M: ?Sized + Serialize,
{
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        let serializer = RedactingSerializer { inner: serializer, redactor: self.redactor };
        self.inner.serialize(serializer)
    }
}

/// Calls [`Redactor::redact_text`] on all strings before forwarding to the inner serializer.
/// Otherwise is identical to using the inner serializer directly.
struct RedactingSerializer<'r, S> {
    redactor: &'r Redactor,
    inner: S,
}

impl<'r, S> Serializer for RedactingSerializer<'r, S>
where
    S: Serializer,
{
    type Ok = S::Ok;
    type Error = S::Error;
    type SerializeSeq = RedactingCompound<'r, S::SerializeSeq>;
    type SerializeTuple = RedactingCompound<'r, S::SerializeTuple>;
    type SerializeTupleStruct = RedactingCompound<'r, S::SerializeTupleStruct>;
    type SerializeTupleVariant = RedactingCompound<'r, S::SerializeTupleVariant>;
    type SerializeMap = RedactingCompound<'r, S::SerializeMap>;
    type SerializeStruct = RedactingCompound<'r, S::SerializeStruct>;
    type SerializeStructVariant = RedactingCompound<'r, S::SerializeStructVariant>;

    fn serialize_str(self, v: &str) -> Result<S::Ok, Self::Error> {
        self.inner.serialize_str(&self.redactor.redact_text(v))
    }

    fn serialize_bool(self, v: bool) -> Result<S::Ok, Self::Error> {
        self.inner.serialize_bool(v)
    }

    fn serialize_i8(self, v: i8) -> Result<S::Ok, Self::Error> {
        self.inner.serialize_i8(v)
    }

    fn serialize_i16(self, v: i16) -> Result<S::Ok, Self::Error> {
        self.inner.serialize_i16(v)
    }

    fn serialize_i32(self, v: i32) -> Result<S::Ok, Self::Error> {
        self.inner.serialize_i32(v)
    }

    fn serialize_i64(self, v: i64) -> Result<S::Ok, Self::Error> {
        self.inner.serialize_i64(v)
    }

    fn serialize_u8(self, v: u8) -> Result<S::Ok, Self::Error> {
        self.inner.serialize_u8(v)
    }

    fn serialize_u16(self, v: u16) -> Result<S::Ok, Self::Error> {
        self.inner.serialize_u16(v)
    }

    fn serialize_u32(self, v: u32) -> Result<S::Ok, Self::Error> {
        self.inner.serialize_u32(v)
    }

    fn serialize_u64(self, v: u64) -> Result<S::Ok, Self::Error> {
        self.inner.serialize_u64(v)
    }

    fn serialize_f32(self, v: f32) -> Result<S::Ok, Self::Error> {
        self.inner.serialize_f32(v)
    }

    fn serialize_f64(self, v: f64) -> Result<S::Ok, Self::Error> {
        self.inner.serialize_f64(v)
    }

    fn serialize_char(self, v: char) -> Result<S::Ok, Self::Error> {
        self.inner.serialize_char(v)
    }

    fn serialize_bytes(self, v: &[u8]) -> Result<S::Ok, Self::Error> {
        self.inner.serialize_bytes(v)
    }

    fn serialize_none(self) -> Result<S::Ok, Self::Error> {
        self.inner.serialize_none()
    }

    fn serialize_some<T>(self, value: &T) -> Result<S::Ok, Self::Error>
    where
        T: ?Sized + Serialize,
    {
        self.inner.serialize_some(&Redacted { inner: value, redactor: self.redactor })
    }

    fn serialize_unit(self) -> Result<S::Ok, Self::Error> {
        self.inner.serialize_unit()
    }

    fn serialize_unit_struct(self, name: &'static str) -> Result<S::Ok, Self::Error> {
        self.inner.serialize_unit_struct(name)
    }

    fn serialize_unit_variant(
        self,
        name: &'static str,
        variant_index: u32,
        variant: &'static str,
    ) -> Result<S::Ok, Self::Error> {
        self.inner.serialize_unit_variant(name, variant_index, variant)
    }

    fn serialize_newtype_struct<T>(
        self,
        name: &'static str,
        value: &T,
    ) -> Result<S::Ok, Self::Error>
    where
        T: ?Sized + Serialize,
    {
        self.inner
            .serialize_newtype_struct(name, &Redacted { inner: value, redactor: self.redactor })
    }

    fn serialize_newtype_variant<T>(
        self,
        name: &'static str,
        variant_index: u32,
        variant: &'static str,
        value: &T,
    ) -> Result<S::Ok, Self::Error>
    where
        T: ?Sized + Serialize,
    {
        self.inner.serialize_newtype_variant(
            name,
            variant_index,
            variant,
            &Redacted { inner: value, redactor: self.redactor },
        )
    }

    fn serialize_seq(self, len: Option<usize>) -> Result<Self::SerializeSeq, Self::Error> {
        Ok(RedactingCompound { inner: self.inner.serialize_seq(len)?, redactor: self.redactor })
    }

    fn serialize_tuple(self, len: usize) -> Result<Self::SerializeTuple, Self::Error> {
        Ok(RedactingCompound { inner: self.inner.serialize_tuple(len)?, redactor: self.redactor })
    }

    fn serialize_tuple_struct(
        self,
        name: &'static str,
        len: usize,
    ) -> Result<Self::SerializeTupleStruct, Self::Error> {
        Ok(RedactingCompound {
            inner: self.inner.serialize_tuple_struct(name, len)?,
            redactor: self.redactor,
        })
    }

    fn serialize_tuple_variant(
        self,
        name: &'static str,
        variant_index: u32,
        variant: &'static str,
        len: usize,
    ) -> Result<Self::SerializeTupleVariant, Self::Error> {
        Ok(RedactingCompound {
            inner: self.inner.serialize_tuple_variant(name, variant_index, variant, len)?,
            redactor: self.redactor,
        })
    }

    fn serialize_map(self, len: Option<usize>) -> Result<Self::SerializeMap, Self::Error> {
        Ok(RedactingCompound { inner: self.inner.serialize_map(len)?, redactor: self.redactor })
    }

    fn serialize_struct(
        self,
        name: &'static str,
        len: usize,
    ) -> Result<Self::SerializeStruct, Self::Error> {
        Ok(RedactingCompound {
            inner: self.inner.serialize_struct(name, len)?,
            redactor: self.redactor,
        })
    }

    fn serialize_struct_variant(
        self,
        name: &'static str,
        variant_index: u32,
        variant: &'static str,
        len: usize,
    ) -> Result<Self::SerializeStructVariant, Self::Error> {
        Ok(RedactingCompound {
            inner: self.inner.serialize_struct_variant(name, variant_index, variant, len)?,
            redactor: self.redactor,
        })
    }
}

pub struct RedactingCompound<'r, S> {
    redactor: &'r Redactor,
    inner: S,
}

impl<'r, S> ser::SerializeSeq for RedactingCompound<'r, S>
where
    S: ser::SerializeSeq,
{
    type Ok = S::Ok;
    type Error = S::Error;

    fn serialize_element<T>(&mut self, value: &T) -> Result<(), Self::Error>
    where
        T: ?Sized + Serialize,
    {
        self.inner.serialize_element(&Redacted { inner: value, redactor: self.redactor })
    }

    fn end(self) -> Result<S::Ok, Self::Error> {
        self.inner.end()
    }
}

impl<'r, S> ser::SerializeTuple for RedactingCompound<'r, S>
where
    S: ser::SerializeTuple,
{
    type Ok = S::Ok;
    type Error = S::Error;

    fn serialize_element<T>(&mut self, value: &T) -> Result<(), Self::Error>
    where
        T: ?Sized + Serialize,
    {
        self.inner.serialize_element(&Redacted { inner: value, redactor: self.redactor })
    }

    fn end(self) -> Result<S::Ok, Self::Error> {
        self.inner.end()
    }
}

impl<'r, S> ser::SerializeTupleStruct for RedactingCompound<'r, S>
where
    S: ser::SerializeTupleStruct,
{
    type Ok = S::Ok;
    type Error = S::Error;

    fn serialize_field<T>(&mut self, value: &T) -> Result<(), Self::Error>
    where
        T: ?Sized + Serialize,
    {
        self.inner.serialize_field(&Redacted { inner: value, redactor: self.redactor })
    }

    fn end(self) -> Result<S::Ok, Self::Error> {
        self.inner.end()
    }
}

impl<'r, S> ser::SerializeTupleVariant for RedactingCompound<'r, S>
where
    S: ser::SerializeTupleVariant,
{
    type Ok = S::Ok;
    type Error = S::Error;

    fn serialize_field<T>(&mut self, value: &T) -> Result<(), Self::Error>
    where
        T: ?Sized + Serialize,
    {
        self.inner.serialize_field(&Redacted { inner: value, redactor: self.redactor })
    }

    fn end(self) -> Result<S::Ok, Self::Error> {
        self.inner.end()
    }
}

impl<'r, S> ser::SerializeMap for RedactingCompound<'r, S>
where
    S: ser::SerializeMap,
{
    type Ok = S::Ok;
    type Error = S::Error;

    fn serialize_key<T>(&mut self, key: &T) -> Result<(), Self::Error>
    where
        T: ?Sized + Serialize,
    {
        self.inner.serialize_key(&Redacted { inner: key, redactor: self.redactor })
    }

    fn serialize_value<T>(&mut self, value: &T) -> Result<(), Self::Error>
    where
        T: ?Sized + Serialize,
    {
        self.inner.serialize_value(&Redacted { inner: value, redactor: self.redactor })
    }

    fn end(self) -> Result<S::Ok, Self::Error> {
        self.inner.end()
    }
}

impl<'r, S> ser::SerializeStruct for RedactingCompound<'r, S>
where
    S: ser::SerializeStruct,
{
    type Ok = S::Ok;
    type Error = S::Error;

    fn serialize_field<T>(&mut self, key: &'static str, value: &T) -> Result<(), Self::Error>
    where
        T: ?Sized + Serialize,
    {
        self.inner.serialize_field(key, &Redacted { inner: value, redactor: self.redactor })
    }

    fn end(self) -> Result<S::Ok, Self::Error> {
        self.inner.end()
    }
}

impl<'r, S> ser::SerializeStructVariant for RedactingCompound<'r, S>
where
    S: ser::SerializeStructVariant,
{
    type Ok = S::Ok;
    type Error = S::Error;

    fn serialize_field<T>(&mut self, key: &'static str, value: &T) -> Result<(), Self::Error>
    where
        T: ?Sized + Serialize,
    {
        self.inner.serialize_field(key, &Redacted { inner: value, redactor: self.redactor })
    }

    fn end(self) -> Result<S::Ok, Self::Error> {
        self.inner.end()
    }
}
