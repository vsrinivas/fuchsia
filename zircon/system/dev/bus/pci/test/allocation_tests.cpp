#include "../allocation.h"
#include <fbl/unique_ptr.h>
#include <unittest/unittest.h>
#include <zircon/limits.h>

namespace pci {

bool StubTest() {
    BEGIN_TEST;

    PciRegionAllocator allocator;
    fbl::unique_ptr<PciAllocation> alloc;

    EXPECT_NE(ZX_OK, allocator.GetRegion(0, ZX_PAGE_SIZE, &alloc));

    END_TEST;
}

BEGIN_TEST_CASE(PciAllocationTests)
RUN_TEST_SMALL(StubTest)
END_TEST_CASE(PciAllocationTests)

} // namespace pci
