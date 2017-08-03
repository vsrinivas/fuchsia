#include <lib/crypto/entropy/quality_test.h>

#include <dev/hw_rng.h>
#include <kernel/cmdline.h>
#include <kernel/vm/vm_object_paged.h>
#include <lk/init.h>
#include <string.h>

namespace crypto {

namespace entropy {

#if ENABLE_ENTROPY_COLLECTOR_TEST

#ifndef ENTROPY_COLLECTOR_TEST_MAXLEN
// Default to 1 million bits (we're a kernel, not a hard disk!)
#define ENTROPY_COLLECTOR_TEST_MAXLEN (128u * 1024u)
#endif

namespace test {

static uint8_t entropy_buf[ENTROPY_COLLECTOR_TEST_MAXLEN];
mxtl::RefPtr<VmObject> entropy_vmo;
bool entropy_was_lost = false;

static void SetupEntropyVmo(uint level) {
    if (VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, sizeof(entropy_buf),
                              &entropy_vmo) != MX_OK) {
        printf("entropy-record: Failed to create entropy_vmo (data lost)\n");
        entropy_was_lost = true;
        return;
    }
    size_t actual;
    if (entropy_vmo->Write(entropy_buf, 0, sizeof(entropy_buf), &actual)
            != MX_OK) {
        printf("entropy-record: Failed to write to entropy_vmo (data lost)\n");
        entropy_was_lost = true;
        return;
    }
    if (actual < sizeof(entropy_buf)) {
        printf("entropy-record: partial write to entropy_vmo (data lost)\n");
        entropy_was_lost = true;
        return;
    }
    constexpr const char *name = "debug/entropy.bin";
    if (entropy_vmo->set_name(name, strlen(name)) != MX_OK) {
        // The name is needed because devmgr uses it to add the VMO as a file in
        // the /boot filesystem.
        printf("entropy-record: could not name entropy_vmo (data lost)\n");
        entropy_was_lost = true;
        return;
    }
}

// Run the entropy collector test.
void TestEntropyCollector() {
    constexpr size_t kMaxRead = 256;
    size_t read, total;
    ssize_t result;

    total = cmdline_get_uint64("kernel.entropy_test.len", sizeof(entropy_buf));
    if (total > sizeof(entropy_buf)) {
        total = sizeof(entropy_buf);
        printf("entropy-record: only recording %zu bytes (try defining "
               "ENTROPY_COLLECTOR_TEST_MAXLEN)\n", sizeof(entropy_buf));
    }

    read = 0;
    while (read < total) {
#if ARCH_X86_64
        result = hw_rng_get_entropy(
                entropy_buf + read,
                mxtl::min(kMaxRead, sizeof(entropy_buf) - read),
                true);
#else
        // Temporary workaround to suppress unused variable warning, only
        // because we don't yet have entropy on ARM.
        (void) kMaxRead;
        result = -1;
#endif

        if (result < 0) {
            // Failed to collect any entropy - report an error and give up.
            printf("entropy-record: source stopped returning entropy after "
                   "%zu bytes.\n", read);
            memset(entropy_buf + read, 0, sizeof(entropy_buf) - read);
            return;
        }
        read += result;
    }
}

} // namespace test

#endif // ENABLE_ENTROPY_COLLECTOR_TEST

} // namespace entropy

} // namespace crypto

#if ENABLE_ENTROPY_COLLECTOR_TEST
LK_INIT_HOOK(setup_entropy_vmo, crypto::entropy::test::SetupEntropyVmo,
             LK_INIT_LEVEL_VM + 1);
#endif
