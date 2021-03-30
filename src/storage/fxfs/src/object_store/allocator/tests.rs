/* TODO: bring back when we have FakeFilesystem.

use {
    super::SimpleAllocator,
    crate::object_store::{allocator::Allocator, filesystem::ObjectManager, Journal, Transaction},
    anyhow::Error,
    fuchsia_async as fasync,
    std::sync::Arc,
};

#[fasync::run_singlethreaded(test)]
async fn test_allocate_reserves() -> Result<(), Error> {
    let objects = Arc::new(ObjectManager::new());
    let journal = Arc::new(Journal::new(objects.clone()));
    let allocator = Arc::new(SimpleAllocator::new(&journal));
    objects.set_allocator(allocator.clone());
    let mut transaction = Transaction::new();
    let allocation1 = allocator.allocate(0, 1, 0, 0..512, &mut transaction).await?;
    let allocation2 = allocator.allocate(0, 1, 0, 0..512, &mut transaction).await?;
    assert!(allocation2.start >= allocation1.end || allocation2.end <= allocation1.start);
    Ok(())
}
*/
