#define	JEMALLOC_PAGES_C_
#include "jemalloc/internal/jemalloc_internal.h"

#ifdef JEMALLOC_SYSCTL_VM_OVERCOMMIT
#include <sys/sysctl.h>
#endif

/******************************************************************************/
/* Data. */

#if !defined(_WIN32) && !defined(__Fuchsia__)
#  define PAGES_PROT_COMMIT (PROT_READ | PROT_WRITE)
#  define PAGES_PROT_DECOMMIT (PROT_NONE)
static int	mmap_flags;
#endif
static bool	os_overcommits;

/******************************************************************************/

#ifdef __Fuchsia__

#include <threads.h>

#include <magenta/process.h>
#include <magenta/status.h>
#include <magenta/syscalls.h>

// Reserve a terabyte of address space for heap allocations.
#define VMAR_SIZE (1ull << 40)

#define MMAP_VMO_NAME "jemalloc-heap"

// malloc wants to manage both address space and memory mapped within
// chunks of address space. To maintain claims to address space we
// must use our own vmar.
static uintptr_t pages_base;
static mx_handle_t pages_vmar;
static mx_handle_t pages_vmo;

// Protect reservations to the pages_vmar.
static mtx_t vmar_lock;

static void* fuchsia_pages_map(void* start, size_t len) {
	if (len >= PTRDIFF_MAX) {
		return NULL;
	}

	// round up to page size
	len = (len + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

	mtx_lock(&vmar_lock);

	// If we are given a base address, then jemalloc's internal
	// bookkeeping expects to be able to extend an allocation at
	// that bit of the address space, and so we just directly
	// compute an offset. If we are not, ask for a new random
	// region from the pages_vmar.

	// TODO(kulakowski) Extending a region might fail. Investigate
	// whether it is worthwhile teaching jemalloc about vmars and
	// vmos at the extent.c or arena.c layer.
	size_t offset;
	if (start != NULL) {
		uintptr_t addr = (uintptr_t)start;
		if (addr < pages_base)
			abort();
		offset = addr - pages_base;
	} else {
		// TODO(kulakowski) Use MG-942 instead of having to
		// allocate and destroy under a lock.
		mx_handle_t subvmar;
		uintptr_t subvmar_base;
		mx_status_t status = _mx_vmar_allocate(pages_vmar, 0u, len,
		    MX_VM_FLAG_CAN_MAP_READ | MX_VM_FLAG_CAN_MAP_WRITE,
		    &subvmar, &subvmar_base);
		if (status != MX_OK)
			abort();
		_mx_vmar_destroy(subvmar);
		_mx_handle_close(subvmar);
		offset = subvmar_base - pages_base;
	}

	uintptr_t ptr = 0;
	uint32_t mx_flags = MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE |
	    MX_VM_FLAG_SPECIFIC;
	mx_status_t status = _mx_vmar_map(pages_vmar, offset, pages_vmo,
	    offset, len, mx_flags, &ptr);
	if (status != MX_OK) {
		ptr = 0u;
	}

	mtx_unlock(&vmar_lock);
	return (void*)ptr;
}

static mx_status_t fuchsia_pages_free(void* addr, size_t size) {
	uintptr_t ptr = (uintptr_t)addr;
	return _mx_vmar_unmap(pages_vmar, ptr, size);
}

static void* fuchsia_pages_trim(void* ret, void* addr, size_t size,
    size_t alloc_size, size_t leadsize) {
	size_t trailsize = alloc_size - leadsize - size;

	if (leadsize != 0)
		pages_unmap(addr, leadsize);
	if (trailsize != 0)
		pages_unmap((void *)((uintptr_t)ret + size), trailsize);
	return (ret);
}

#endif

void *
pages_map(void *addr, size_t size, bool *commit)
{
	void *ret;

	assert(size != 0);

	if (os_overcommits)
		*commit = true;

#ifdef _WIN32
	/*
	 * If VirtualAlloc can't allocate at the given address when one is
	 * given, it fails and returns NULL.
	 */
	ret = VirtualAlloc(addr, size, MEM_RESERVE | (*commit ? MEM_COMMIT : 0),
	    PAGE_READWRITE);
#elif __Fuchsia__
	ret = fuchsia_pages_map(addr, size);
#else
	/*
	 * We don't use MAP_FIXED here, because it can cause the *replacement*
	 * of existing mappings, and we only want to create new mappings.
	 */
	{
		int prot = *commit ? PAGES_PROT_COMMIT : PAGES_PROT_DECOMMIT;

		ret = mmap(addr, size, prot, mmap_flags, -1, 0);
	}
	assert(ret != NULL);

	if (ret == MAP_FAILED)
		ret = NULL;
	else if (addr != NULL && ret != addr) {
		/*
		 * We succeeded in mapping memory, but not in the right place.
		 */
		pages_unmap(ret, size);
		ret = NULL;
	}
#endif
	assert(ret == NULL || (addr == NULL && ret != addr)
	    || (addr != NULL && ret == addr));
	return (ret);
}

void
pages_unmap(void *addr, size_t size)
{
#ifdef _WIN32
	if (VirtualFree(addr, 0, MEM_RELEASE) == 0)
#elif __Fuchsia__
	mx_status_t status = fuchsia_pages_free(addr, size);
	if (status != MX_OK)
#else
	if (munmap(addr, size) == -1)
#endif
	{
#if __Fuchsia__
		const char* buf = _mx_status_get_string(status);
#else
		char buf[BUFERROR_BUF];
		buferror(get_errno(), buf, sizeof(buf));
#endif

		malloc_printf("<jemalloc>: Error in "
#ifdef _WIN32
		              "VirtualFree"
#elif __Fuchsia__
		              "unmapping jemalloc heap pages"
#else
		              "munmap"
#endif
		              "(): %s\n", buf);
		if (opt_abort)
			abort();
	}
}

void *
pages_trim(void *addr, size_t alloc_size, size_t leadsize, size_t size,
    bool *commit)
{
	void *ret = (void *)((uintptr_t)addr + leadsize);

	assert(alloc_size >= leadsize + size);
#ifdef _WIN32
	{
		void *new_addr;

		pages_unmap(addr, alloc_size);
		new_addr = pages_map(ret, size, commit);
		if (new_addr == ret)
			return (ret);
		if (new_addr)
			pages_unmap(new_addr, size);
		return (NULL);
	}
#elif __Fuchsia__
	return fuchsia_pages_trim(ret, addr, size, alloc_size, leadsize);
#else
	{
		size_t trailsize = alloc_size - leadsize - size;

		if (leadsize != 0)
			pages_unmap(addr, leadsize);
		if (trailsize != 0)
			pages_unmap((void *)((uintptr_t)ret + size), trailsize);
		return (ret);
	}
#endif
}

static bool
pages_commit_impl(void *addr, size_t size, bool commit)
{
	if (os_overcommits)
		return (true);

#ifdef _WIN32
	return (commit ? (addr != VirtualAlloc(addr, size, MEM_COMMIT,
	    PAGE_READWRITE)) : (!VirtualFree(addr, size, MEM_DECOMMIT)));
#elif __Fuchsia__
	not_reached();
#else
	{
		int prot = commit ? PAGES_PROT_COMMIT : PAGES_PROT_DECOMMIT;
		void *result = mmap(addr, size, prot, mmap_flags | MAP_FIXED,
		    -1, 0);
		if (result == MAP_FAILED)
			return (true);
		if (result != addr) {
			/*
			 * We succeeded in mapping memory, but not in the right
			 * place.
			 */
			pages_unmap(result, size);
			return (true);
		}
		return (false);
	}
#endif
}

bool
pages_commit(void *addr, size_t size)
{
	return (pages_commit_impl(addr, size, true));
}

bool
pages_decommit(void *addr, size_t size)
{
	return (pages_commit_impl(addr, size, false));
}

bool
pages_purge_lazy(void *addr, size_t size)
{
	if (!pages_can_purge_lazy)
		return (true);

#ifdef _WIN32
	VirtualAlloc(addr, size, MEM_RESET, PAGE_READWRITE);
#elif defined(JEMALLOC_PURGE_MADVISE_FREE)
	madvise(addr, size, MADV_FREE);
#else
	not_reached();
#endif
	return (false);
}

bool
pages_purge_forced(void *addr, size_t size)
{
	if (!pages_can_purge_forced)
		return (true);

#if defined(JEMALLOC_PURGE_MADVISE_DONTNEED)
	return (madvise(addr, size, MADV_DONTNEED) != 0);
#else
	not_reached();
#endif
}

bool
pages_huge(void *addr, size_t size)
{
	assert(HUGEPAGE_ADDR2BASE(addr) == addr);
	assert(HUGEPAGE_CEILING(size) == size);

#ifdef JEMALLOC_THP
	return (madvise(addr, size, MADV_HUGEPAGE) != 0);
#else
	return (true);
#endif
}

bool
pages_nohuge(void *addr, size_t size)
{
	assert(HUGEPAGE_ADDR2BASE(addr) == addr);
	assert(HUGEPAGE_CEILING(size) == size);

#ifdef JEMALLOC_THP
	return (madvise(addr, size, MADV_NOHUGEPAGE) != 0);
#else
	return (false);
#endif
}

#ifdef JEMALLOC_SYSCTL_VM_OVERCOMMIT
static bool
os_overcommits_sysctl(void)
{
	int vm_overcommit;
	size_t sz;

	sz = sizeof(vm_overcommit);
	if (sysctlbyname("vm.overcommit", &vm_overcommit, &sz, NULL, 0) != 0)
		return (false); /* Error. */

	return ((vm_overcommit & 0x3) == 0);
}
#endif

#ifdef JEMALLOC_PROC_SYS_VM_OVERCOMMIT_MEMORY
/*
 * Use syscall(2) rather than {open,read,close}(2) when possible to avoid
 * reentry during bootstrapping if another library has interposed system call
 * wrappers.
 */
static bool
os_overcommits_proc(void)
{
	int fd;
	char buf[1];
	ssize_t nread;

#if defined(JEMALLOC_USE_SYSCALL) && defined(SYS_open)
	fd = (int)syscall(SYS_open, "/proc/sys/vm/overcommit_memory", O_RDONLY);
#else
	fd = open("/proc/sys/vm/overcommit_memory", O_RDONLY);
#endif
	if (fd == -1)
		return (false); /* Error. */

#if defined(JEMALLOC_USE_SYSCALL) && defined(SYS_read)
	nread = (ssize_t)syscall(SYS_read, fd, &buf, sizeof(buf));
#else
	nread = read(fd, &buf, sizeof(buf));
#endif

#if defined(JEMALLOC_USE_SYSCALL) && defined(SYS_close)
	syscall(SYS_close, fd);
#else
	close(fd);
#endif

	if (nread < 1)
		return (false); /* Error. */
	/*
	 * /proc/sys/vm/overcommit_memory meanings:
	 * 0: Heuristic overcommit.
	 * 1: Always overcommit.
	 * 2: Never overcommit.
	 */
	return (buf[0] == '0' || buf[0] == '1');
}
#endif

void
pages_boot(void)
{
#if !defined(_WIN32) && !defined(__Fuchsia__)
	mmap_flags = MAP_PRIVATE | MAP_ANON;
#endif

#if defined(__Fuchsia__)
	uint32_t vmar_flags = MX_VM_FLAG_CAN_MAP_SPECIFIC | MX_VM_FLAG_CAN_MAP_READ |
	    MX_VM_FLAG_CAN_MAP_WRITE;
	mx_status_t status = _mx_vmar_allocate(_mx_vmar_root_self(), 0, VMAR_SIZE,
					       vmar_flags, &pages_vmar, &pages_base);
	if (status != MX_OK)
		abort();
	status = _mx_vmo_create(VMAR_SIZE, 0, &pages_vmo);
	if (status != MX_OK)
		abort();
	status = _mx_object_set_property(pages_vmo, MX_PROP_NAME, MMAP_VMO_NAME,
	    strlen(MMAP_VMO_NAME));
	if (status != MX_OK)
		abort();
#endif

#if defined(__Fuchsia__)
	os_overcommits = true;
#elif defined(JEMALLOC_SYSCTL_VM_OVERCOMMIT)
	os_overcommits = os_overcommits_sysctl();
#elif defined(JEMALLOC_PROC_SYS_VM_OVERCOMMIT_MEMORY)
	os_overcommits = os_overcommits_proc();
#  ifdef MAP_NORESERVE
	if (os_overcommits)
		mmap_flags |= MAP_NORESERVE;
#  endif
#else
	os_overcommits = false;
#endif
}
