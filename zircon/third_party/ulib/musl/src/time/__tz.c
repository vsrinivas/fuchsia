#include "libc.h"
#include "time_impl.h"
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

// TODO(cpu): Only UTC is supported so the code in this file can greatly
// simplified.

static long __timezone = 0;
static int __daylight = 0;
static char* __tzname[2] = {0, 0};

weak_alias(__timezone, timezone);
weak_alias(__daylight, daylight);
weak_alias(__tzname, tzname);

static char std_name[TZNAME_MAX + 1];
static char dst_name[TZNAME_MAX + 1];

const char __gmt[] = "GMT";

static int dst_off;
static int r0[5], r1[5];

static const unsigned char *zi, *trans, *idx, *types, *abbrevs, *abbrevs_end;

static mtx_t lock;

static int getint(const char** p) {
    unsigned x;
    for (x = 0; **p - '0' < 10U; (*p)++)
        x = **p - '0' + 10 * x;
    return x;
}

static int getoff(const char** p) {
    int neg = 0;
    if (**p == '-') {
        ++*p;
        neg = 1;
    } else if (**p == '+') {
        ++*p;
    }
    int off = 3600 * getint(p);
    if (**p == ':') {
        ++*p;
        off += 60 * getint(p);
        if (**p == ':') {
            ++*p;
            off += getint(p);
        }
    }
    return neg ? -off : off;
}

static void getrule(const char** p, int rule[5]) {
    int r = rule[0] = **p;

    if (r != 'M') {
        if (r == 'J')
            ++*p;
        else
            rule[0] = 0;
        rule[1] = getint(p);
    } else {
        ++*p;
        rule[1] = getint(p);
        ++*p;
        rule[2] = getint(p);
        ++*p;
        rule[3] = getint(p);
    }

    if (**p == '/') {
        ++*p;
        rule[4] = getoff(p);
    } else {
        rule[4] = 7200;
    }
}

static void getname(char* d, const char** p) {
    int i;
    if (**p == '<') {
        ++*p;
        for (i = 0; **p != '>' && i < TZNAME_MAX; i++)
            d[i] = (*p)[i];
        ++*p;
    } else {
        for (i = 0; ((*p)[i] | 32) - 'a' < 26U && i < TZNAME_MAX; i++)
            d[i] = (*p)[i];
    }
    *p += i;
    d[i] = 0;
}

#define VEC(...) ((const unsigned char[]){__VA_ARGS__})

static uint32_t zi_read32(const unsigned char* z) {
    return (unsigned)z[0] << 24 | z[1] << 16 | z[2] << 8 | z[3];
}

static size_t zi_dotprod(const unsigned char* z, const unsigned char* v, size_t n) {
    size_t y;
    uint32_t x;
    for (y = 0; n; n--, z += 4, v++) {
        x = zi_read32(z);
        y += x * *v;
    }
    return y;
}

int __munmap(void*, size_t);

static void do_tzset(void) {
    const char* s =__gmt;
    getname(std_name, &s);
    __tzname[0] = std_name;
    __timezone = getoff(&s);
    getname(dst_name, &s);
    __tzname[1] = dst_name;
    if (dst_name[0]) {
        __daylight = 1;
        if (*s == '+' || *s == '-' || *s - '0' < 10U)
            dst_off = getoff(&s);
        else
            dst_off = __timezone - 3600;
    } else {
        __daylight = 0;
        dst_off = 0;
    }

    if (*s == ',')
        s++, getrule(&s, r0);
    if (*s == ',')
        s++, getrule(&s, r1);
}

/* Search zoneinfo rules to find the one that applies to the given time,
 * and determine alternate opposite-DST-status rule that may be needed. */

static size_t scan_trans(long long t, int local, size_t* alt) {
    int scale = 3 - (trans == zi + 44);
    uint64_t x;
    int off = 0;

    size_t a = 0, n = (idx - trans) >> scale, m;

    if (!n) {
        if (alt)
            *alt = 0;
        return 0;
    }

    /* Binary search for 'most-recent rule before t'. */
    while (n > 1) {
        m = a + n / 2;
        x = zi_read32(trans + (m << scale));
        if (scale == 3)
            x = x << 32 | zi_read32(trans + (m << scale) + 4);
        else
            x = (int32_t)x;
        if (local)
            off = (int32_t)zi_read32(types + 6 * idx[m - 1]);
        if (t - off < (int64_t)x) {
            n /= 2;
        } else {
            a = m;
            n -= n / 2;
        }
    }

    /* First and last entry are special. First means to use lowest-index
     * non-DST type. Last means to apply POSIX-style rule if available. */
    n = (idx - trans) >> scale;
    if (a == n - 1)
        return -1;
    if (a == 0) {
        x = zi_read32(trans + (a << scale));
        if (scale == 3)
            x = x << 32 | zi_read32(trans + (a << scale) + 4);
        else
            x = (int32_t)x;
        if (local)
            off = (int32_t)zi_read32(types + 6 * idx[a - 1]);
        if (t - off < (int64_t)x) {
            for (a = 0; a < (abbrevs - types) / 6; a++) {
                if (types[6 * a + 4] != types[4])
                    break;
            }
            if (a == (abbrevs - types) / 6)
                a = 0;
            if (types[6 * a + 4]) {
                *alt = a;
                return 0;
            } else {
                *alt = 0;
                return a;
            }
        }
    }

    /* Try to find a neighboring opposite-DST-status rule. */
    if (alt) {
        if (a && types[6 * idx[a - 1] + 4] != types[6 * idx[a] + 4])
            *alt = idx[a - 1];
        else if (a + 1 < n && types[6 * idx[a + 1] + 4] != types[6 * idx[a] + 4])
            *alt = idx[a + 1];
        else
            *alt = idx[a];
    }

    return idx[a];
}

static int days_in_month(int m, int is_leap) {
    if (m == 2)
        return 28 + is_leap;
    else
        return 30 + ((0xad5 >> (m - 1)) & 1);
}

/* Convert a POSIX DST rule plus year to seconds since epoch. */

static long long rule_to_secs(const int* rule, int year) {
    int is_leap;
    long long t = __year_to_secs(year, &is_leap);
    int x, m, n, d;
    if (rule[0] != 'M') {
        x = rule[1];
        if (rule[0] == 'J' && (x < 60 || !is_leap))
            x--;
        t += 86400 * x;
    } else {
        m = rule[1];
        n = rule[2];
        d = rule[3];
        t += __month_to_secs(m - 1, is_leap);
        int wday = (int)((t + 4 * 86400) % (7 * 86400)) / 86400;
        int days = d - wday;
        if (days < 0)
            days += 7;
        if (n == 5 && days + 28 >= days_in_month(m, is_leap))
            n = 4;
        t += 86400 * (days + 7 * (n - 1));
    }
    t += rule[4];
    return t;
}

/* Determine the time zone in effect for a given time in seconds since the
 * epoch. It can be given in local or universal time. The results will
 * indicate whether DST is in effect at the queried time, and will give both
 * the GMT offset for the active zone/DST rule and the opposite DST. This
 * enables a caller to efficiently adjust for the case where an explicit
 * DST specification mismatches what would be in effect at the time. */

void __secs_to_zone(long long t, int local, int* isdst, long* offset, long* oppoff,
                    const char** zonename) {
    mtx_lock(&lock);

    do_tzset();

    if (zi) {
        size_t alt, i = scan_trans(t, local, &alt);
        if (i != -1) {
            *isdst = types[6 * i + 4];
            *offset = (int32_t)zi_read32(types + 6 * i);
            *zonename = (const char*)abbrevs + types[6 * i + 5];
            if (oppoff)
                *oppoff = (int32_t)zi_read32(types + 6 * alt);
            mtx_unlock(&lock);
            return;
        }
    }

    if (!__daylight)
        goto std;

    /* FIXME: may be broken if DST changes right at year boundary?
     * Also, this could be more efficient.*/
    long long y = t / 31556952 + 70;
    while (__year_to_secs(y, 0) > t)
        y--;
    while (__year_to_secs(y + 1, 0) < t)
        y++;

    long long t0 = rule_to_secs(r0, y);
    long long t1 = rule_to_secs(r1, y);

    if (t0 < t1) {
        if (!local) {
            t0 += __timezone;
            t1 += dst_off;
        }
        if (t >= t0 && t < t1)
            goto dst;
        goto std;
    } else {
        if (!local) {
            t1 += __timezone;
            t0 += dst_off;
        }
        if (t >= t1 && t < t0)
            goto std;
        goto dst;
    }
std:
    *isdst = 0;
    *offset = -__timezone;
    if (oppoff)
        *oppoff = -dst_off;
    *zonename = __tzname[0];
    mtx_unlock(&lock);
    return;
dst:
    *isdst = 1;
    *offset = -dst_off;
    if (oppoff)
        *oppoff = -__timezone;
    *zonename = __tzname[1];
    mtx_unlock(&lock);
}

void __tzset(void) {
    mtx_lock(&lock);
    do_tzset();
    mtx_unlock(&lock);
}

weak_alias(__tzset, tzset);

const char* __tm_to_tzname(const struct tm* tm) {
    const void* p = tm->__tm_zone;
    mtx_lock(&lock);
    do_tzset();
    if (p != __gmt && p != __tzname[0] && p != __tzname[1] &&
        (!zi || (uintptr_t)p - (uintptr_t)abbrevs >= abbrevs_end - abbrevs))
        p = "";
    mtx_unlock(&lock);
    return p;
}
