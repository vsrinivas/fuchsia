use {
    crate::object_store::Transaction,
    std::{borrow::Borrow, io::Result, marker::PhantomData, ops::Range, vec::Vec},
};

// TODO: this should change to be an async trait.
pub trait ObjectHandle: Send + Sync {
    fn object_id(&self) -> u64;
    fn read(&self, offset: u64, buf: &mut [u8]) -> Result<usize>;
    fn write(&self, offset: u64, buf: &[u8]) -> Result<()>;
    fn get_size(&self) -> u64;
    fn preallocate_range(
        &self,
        range: Range<u64>,
        transaction: &mut Transaction,
    ) -> Result<Vec<Range<u64>>>;
}

pub struct ObjectHandleCursor<'cursor, H: Borrow<dyn ObjectHandle + 'cursor>> {
    handle: H,
    pos: u64,
    phantom: std::marker::PhantomData<&'cursor H>,
}

impl<'cursor, H: Borrow<dyn ObjectHandle + 'cursor>> ObjectHandleCursor<'cursor, H> {
    pub fn new(handle: H, pos: u64) -> ObjectHandleCursor<'cursor, H> {
        ObjectHandleCursor { handle, pos, phantom: PhantomData }
    }

    // pub fn pos(&self) -> u64 { self.pos }
}

impl<'cursor, H: Borrow<dyn ObjectHandle + 'cursor>> std::io::Read
    for ObjectHandleCursor<'cursor, H>
{
    fn read(&mut self, buf: &mut [u8]) -> Result<usize> {
        let read = self.handle.borrow().read(self.pos, buf)?;
        self.pos += read as u64;
        Ok(read)
    }
}

impl<'cursor, H: Borrow<dyn ObjectHandle + 'cursor>> std::io::Write
    for ObjectHandleCursor<'cursor, H>
{
    fn write(&mut self, buf: &[u8]) -> Result<usize> {
        // println!("writing {:?} @ {:?}", buf.len(), self.pos);
        self.handle.borrow().write(self.pos, buf)?;
        self.pos += buf.len() as u64;
        Ok(buf.len())
    }

    fn flush(&mut self) -> Result<()> {
        Ok(())
    }
}
