#include <kernel/vm/vm_object.h>
#include <mxtl/ref_ptr.h>

namespace crypto {

namespace entropy {

#if ENABLE_ENTROPY_COLLECTOR_TEST
namespace test {

extern mxtl::RefPtr<VmObject> entropy_vmo;
extern bool entropy_was_lost;

void TestEntropyCollector();

} // namespace test
#endif

} // namespace entropy

} // namespace crypto

