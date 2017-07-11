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

#include <magenta/process.h>
#include <magenta/syscalls.h>

static const char mmap_vmo_name[] = "jemalloc-heap";

void* fuchsia_pages_map(void* start, size_t len, bool commit, bool fixed) {
	uint32_t mx_flags = 0u;
	if (commit)
		mx_flags |= (MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE);
	if (fixed)
		mx_flags |= MX_VM_FLAG_SPECIFIC;

	if (len == 0) {
		errno = EINVAL;
		return NULL;
	}
	if (len >= PTRDIFF_MAX) {
		errno = ENOMEM;
		return NULL;
	}

	// round up to page size
	len = (len + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

	size_t offset = 0;
	mx_status_t status = MX_OK;
	if (fixed) {
		mx_info_vmar_t info;
		status = _mx_object_get_info(_mx_vmar_root_self(), MX_INFO_VMAR, &info,
		    sizeof(info), NULL, NULL);
		if (status < 0 || (uintptr_t)start < info.base) {
			goto fail;
		}
		offset = (uintptr_t)start - info.base;
	}

	mx_handle_t vmo;
	uintptr_t ptr = 0;
	if (_mx_vmo_create(len, 0, &vmo) < 0) {
		errno = ENOMEM;
		return NULL;
	}
	_mx_object_set_property(vmo, MX_PROP_NAME, mmap_vmo_name, strlen(mmap_vmo_name));

	status = _mx_vmar_map(_mx_vmar_root_self(), offset, vmo, 0u, len, mx_flags, &ptr);
	_mx_handle_close(vmo);
	if (status < 0) {
		goto fail;
	}

	return (void*)ptr;

fail:
	switch (status) {
	case MX_ERR_BAD_HANDLE:
		errno = EBADF;
		break;
	case MX_ERR_NOT_SUPPORTED:
		errno = ENODEV;
		break;
	case MX_ERR_ACCESS_DENIED:
		errno = EACCES;
		break;
	case MX_ERR_NO_MEMORY:
		errno = ENOMEM;
		break;
	case MX_ERR_INVALID_ARGS:
	case MX_ERR_BAD_STATE:
	default:
		errno = EINVAL;
	}
	return NULL;
}

static void* fuchsia_pages_alloc(void* addr, size_t size, bool commit) {
	/*
	 * We don't use fixed=false here, because it can cause the *replacement*
	 * of existing mappings, and we only want to create new mappings.
	 */
	void* ret = fuchsia_pages_map(addr, size, commit, /* fixed */ false);
	if (addr != NULL && ret != addr && ret != NULL) {
		/*
		 * We succeeded in mapping memory, but not in the right place.
		 */
		pages_unmap(ret, size);
		ret = NULL;
	}
	return ret;
}

static int fuchsia_pages_free(void* addr, size_t size) {
	uintptr_t ptr = (uintptr_t)addr;
	mx_status_t status = _mx_vmar_unmap(_mx_vmar_root_self(), ptr, size);
	if (status < 0) {
		errno = EINVAL;
		return -1;
	}
	return 0;
}

static void* fuchsia_pages_trim(void* ret, void* addr, size_t size, size_t alloc_size, size_t leadsize) {
	size_t trailsize = alloc_size - leadsize - size;

	if (leadsize != 0)
		pages_unmap(addr, leadsize);
	if (trailsize != 0)
		pages_unmap((void *)((uintptr_t)ret + size), trailsize);
	return (ret);
}

static bool fuchsia_pages_commit(void* addr, size_t size, bool commit) {
	void *result = fuchsia_pages_map(addr, size, commit, /* fixed */ true);
	if (result == NULL)
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
	ret = fuchsia_pages_alloc(addr, size, *commit);
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
	if (fuchsia_pages_free(addr, size) == -1)
#else
	if (munmap(addr, size) == -1)
#endif
	{
		char buf[BUFERROR_BUF];

		buferror(get_errno(), buf, sizeof(buf));
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
	return fuchsia_pages_commit(addr, size, commit);
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

#ifdef JEMALLOC_SYSCTL_VM_OVERCOMMIT
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
