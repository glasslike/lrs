#include "util.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

/* The log file, or NULL when only stderr is in use. One process, so
 * this does not need a lock of its own. The mailer and GoldED never
 * read it. */
static FILE *log_fp;

void log_open(const char *path)
{
    if (log_fp) {
        fclose(log_fp);
        log_fp = NULL;
    }
    if (!path || !path[0])
        return;
    /* "a" creates the file. On Windows it also turns \n into \r\n,
     * which is what a text log wants. The directory must already exist. */
    log_fp = fopen(path, "a");
    if (!log_fp)
        fprintf(stderr, "lrs: cannot open log %s\n", path);
}

void log_close(void)
{
    if (!log_fp)
        return;
    fclose(log_fp);
    log_fp = NULL;
}

void log_msg(const char *fmt, ...)
{
    char body[LRP_LINE];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(body, sizeof body, fmt, ap);
    va_end(ap);
    if (n < 0) {
        body[0] = '\0';
    } else if ((size_t)n >= sizeof body) {
        /* Keep a truncated line rather than dropping the event. */
        body[sizeof body - 1] = '\0';
    }
    fprintf(stderr, "lrs: %s\n", body);
    if (log_fp) {
        time_t now = time(NULL);
        struct tm *tm = localtime(&now);
        if (tm) {
            fprintf(log_fp, "%04d-%02d-%02d %02d:%02d:%02d %s\n",
                tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                tm->tm_hour, tm->tm_min, tm->tm_sec, body);
        } else {
            fprintf(log_fp, "%s\n", body);
        }
        /* Flush every line. A cron job that is killed mid-write should
         * still leave the last completed event on disk. */
        fflush(log_fp);
    }
}

int str_icmp(const char *a, const char *b)
{
    unsigned char ca;
    unsigned char cb;
    if (!a || !b)
        return (a != NULL) - (b != NULL);
    do {
        ca = (unsigned char)tolower((unsigned char)*a++);
        cb = (unsigned char)tolower((unsigned char)*b++);
    } while (ca && ca == cb);
    return (int)ca - (int)cb;
}

void str_trim(char *s)
{
    char *end;
    char *start;
    size_t n;
    if (!s)
        return;
    start = s;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n')
        start++;
    n = strlen(start);
    end = start + n;
    while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n'))
        end--;
    *end = '\0';
    if (start != s)
        memmove(s, start, (size_t)(end - start) + 1);
}

char *str_dup(const char *s)
{
    size_t n;
    char *d;
    if (!s)
        return NULL;
    n = strlen(s);
    d = (char *)malloc(n + 1);
    if (!d)
        return NULL;
    memcpy(d, s, n + 1);
    return d;
}

int path_join(char *dst, size_t cap, const char *dir, const char *name)
{
    size_t n;
    int slash;
    if (!dst || cap == 0 || !dir || !name)
        return -1;
    n = strlen(dir);
    slash = (n > 0 && (dir[n - 1] == '/' || dir[n - 1] == '\\'));
    if (snprintf(dst, cap, "%s%s%s", dir, slash ? "" : "/", name) >= (int)cap)
        return -1;
    return 0;
}

const char *path_base(const char *path)
{
    const char *s;
    const char *last;
    if (!path)
        return "";
    last = path;
    for (s = path; *s; s++) {
        if (*s == '/' || *s == '\\')
            last = s + 1;
    }
    return last;
}

int path_exists(const char *path)
{
#ifdef _WIN32
    DWORD a;
    if (!path || !*path)
        return 0;
    a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES;
#else
    struct stat st;
    if (!path || !*path)
        return 0;
    return stat(path, &st) == 0;
#endif
}

int path_mtime(const char *path, int64_t *mtime)
{
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA d;
    ULARGE_INTEGER u;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &d))
        return -1;
    u.LowPart = d.ftLastWriteTime.dwLowDateTime;
    u.HighPart = d.ftLastWriteTime.dwHighDateTime;
    /* FILETIME is 100 ns ticks since 1601-01-01. */
    if (mtime)
        *mtime = (int64_t)(u.QuadPart / 10000000ULL) - 11644473600LL;
    return 0;
#else
    struct stat st;
    if (stat(path, &st) != 0)
        return -1;
    if (mtime)
        *mtime = (int64_t)st.st_mtime;
    return 0;
#endif
}

uint16_t rd_u16(const unsigned char *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

uint32_t rd_u32(const unsigned char *p)
{
    return (uint32_t)p[0]
        | ((uint32_t)p[1] << 8)
        | ((uint32_t)p[2] << 16)
        | ((uint32_t)p[3] << 24);
}

void wr_u32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v & 0xff);
    p[1] = (unsigned char)((v >> 8) & 0xff);
    p[2] = (unsigned char)((v >> 16) & 0xff);
    p[3] = (unsigned char)((v >> 24) & 0xff);
}

int file_sync(FILE *fp)
{
    if (!fp || fflush(fp) != 0)
        return -1;
#ifdef _WIN32
    return _commit(_fileno(fp)) == 0 ? 0 : -1;
#else
    return fsync(fileno(fp)) == 0 ? 0 : -1;
#endif
}

int write_atomic(const char *path, const char *text)
{
    char tmp[LRP_PATH + 8];
    FILE *fp;
    size_t n;
    if (!path || !text)
        return -1;
    if (snprintf(tmp, sizeof tmp, "%s.tmp", path) >= (int)sizeof tmp)
        return -1;
    fp = fopen(tmp, "wb");
    if (!fp)
        return -1;
    n = strlen(text);
    if (n && fwrite(text, 1, n, fp) != n) {
        fclose(fp);
        remove(tmp);
        return -1;
    }
    /* The replace only happens after the bytes are durable. A crash
     * before that leaves the previous file in place. The destination
     * is never deleted first: a failed replace must not erase it. */
    if (file_sync(fp) != 0) {
        fclose(fp);
        remove(tmp);
        return -1;
    }
    if (fclose(fp) != 0) {
        remove(tmp);
        return -1;
    }
#ifdef _WIN32
    /* Replace can lose a race with a scanner that opened the previous
     * file for a moment. The old bytes are still in place. A few short
     * waits are enough; a hard error is not retried. */
    {
        int attempt;
        for (attempt = 0; attempt < 8; attempt++) {
            if (MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                return 0;
            if (GetLastError() != ERROR_ACCESS_DENIED
                && GetLastError() != ERROR_SHARING_VIOLATION)
                break;
            Sleep(25);
        }
        log_msg("cannot replace %s (%lu)", path, (unsigned long)GetLastError());
        remove(tmp);
        return -1;
    }
#else
    if (rename(tmp, path) != 0) {
        remove(tmp);
        return -1;
    }
#endif
    return 0;
}

/* Snapshots, state and the config are text. A larger file is refused
 * instead of being allocated: it is not one of these. */
#define READ_MAX (8u * 1024u * 1024u)

char *read_all(const char *path, size_t *len)
{
    FILE *fp;
    long sz;
    char *buf;
    fp = fopen(path, "rb");
    if (!fp)
        return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    sz = ftell(fp);
    if (sz < 0 || (unsigned long)sz > READ_MAX) {
        fclose(fp);
        if (sz > (long)READ_MAX)
            log_msg("%s is too large", path ? path : "");
        return NULL;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }
    buf = (char *)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    if (sz > 0 && fread(buf, 1, (size_t)sz, fp) != (size_t)sz) {
        free(buf);
        fclose(fp);
        return NULL;
    }
    buf[sz] = '\0';
    fclose(fp);
    if (len)
        *len = (size_t)sz;
    return buf;
}

int addr_parse(const char *s, unsigned *zone, unsigned *net, unsigned *node, unsigned *point)
{
    unsigned z = 0, n = 0, f = 0, p = 0;
    const char *c;
    char *end;
    if (!s || !*s)
        return -1;
    z = (unsigned)strtoul(s, &end, 10);
    if (end == s || *end != ':')
        return -1;
    c = end + 1;
    n = (unsigned)strtoul(c, &end, 10);
    if (end == c || *end != '/')
        return -1;
    c = end + 1;
    f = (unsigned)strtoul(c, &end, 10);
    if (end == c)
        return -1;
    if (*end == '.') {
        c = end + 1;
        p = (unsigned)strtoul(c, &end, 10);
        if (end == c)
            return -1;
    }
    if (*end != '\0')
        return -1;
    if (zone) *zone = z;
    if (net) *net = n;
    if (node) *node = f;
    if (point) *point = p;
    return 0;
}

int addr_cmp_str(const char *a, const char *b)
{
    unsigned az, an, af, ap, bz, bn, bf, bp;
    if (addr_parse(a, &az, &an, &af, &ap) != 0 || addr_parse(b, &bz, &bn, &bf, &bp) != 0)
        return str_icmp(a ? a : "", b ? b : "");
    if (az != bz) return az < bz ? -1 : 1;
    if (an != bn) return an < bn ? -1 : 1;
    if (af != bf) return af < bf ? -1 : 1;
    if (ap != bp) return ap < bp ? -1 : 1;
    return 0;
}

int bso_flo_bsy(const char *outbound, const char *peer,
    char *flo, size_t flo_cap, char *bsy, size_t bsy_cap)
{
    unsigned pz, pn, pf, pp;
    char root[LRP_PATH];
    size_t n;
    int nw;

    /* pz is parsed so a zone:net/node address is accepted, then ignored.
     * Both partners share one configured outbound; the zone never selects
     * another directory. */
    if (addr_parse(peer, &pz, &pn, &pf, &pp) != 0)
        return -1;
    (void)pz;
    if (!outbound || !outbound[0] || !flo || !bsy)
        return -1;

    /* A trailing separator would produce "outbound//00cb038e.flo". */
    n = strlen(outbound);
    while (n > 0 && (outbound[n - 1] == '/' || outbound[n - 1] == '\\'))
        n--;
    if (n == 0 || n >= sizeof root)
        return -1;
    memcpy(root, outbound, n);
    root[n] = '\0';

    /* Point 0 is a node: one file, net and node packed into eight hex
     * digits (203/910 is 00cb038e). A point adds qico's ".pnt" directory
     * and an eight-digit point, so 2:203/910.1 is
     * 00cb038e.pnt/00000001.flo. The busy file is the same stem with .bsy.
     * Both files stay under the outbound directory from the config. */
    if (pp) {
        nw = snprintf(flo, flo_cap, "%s/%04x%04x.pnt/%08x.flo", root, pn, pf, pp);
        if (nw < 0 || (size_t)nw >= flo_cap)
            return -1;
        nw = snprintf(bsy, bsy_cap, "%s/%04x%04x.pnt/%08x.bsy", root, pn, pf, pp);
    } else {
        nw = snprintf(flo, flo_cap, "%s/%04x%04x.flo", root, pn, pf);
        if (nw < 0 || (size_t)nw >= flo_cap)
            return -1;
        nw = snprintf(bsy, bsy_cap, "%s/%04x%04x.bsy", root, pn, pf);
    }
    if (nw < 0 || (size_t)nw >= bsy_cap)
        return -1;
    return 0;
}

/* One path component. EEXIST is success: another process may have
 * created it between the exists check and the call. A last exists
 * check covers the case where mkdir fails because the directory was
 * already there but errno is not EEXIST. */
static int mkdir_one(const char *path)
{
    if (path_exists(path))
        return 0;
#ifdef _WIN32
    if (_mkdir(path) == 0 || errno == EEXIST)
        return 0;
#else
    if (mkdir(path, 0755) == 0 || errno == EEXIST)
        return 0;
#endif
    return path_exists(path) ? 0 : -1;
}

int path_ensure_parent(const char *filepath)
{
    char buf[LRP_PATH];
    char *p;
    size_t i;
    if (!filepath || !filepath[0])
        return -1;
    if (strlen(filepath) >= sizeof buf)
        return -1;
    strcpy(buf, filepath);
    /* Drop the filename. A path with no directory is already usable. */
    p = NULL;
    for (i = 0; buf[i]; i++) {
        if (buf[i] == '/' || buf[i] == '\\')
            p = buf + i;
    }
    if (!p || p == buf)
        return 0;
    *p = '\0';
    /* Create each prefix in order. "D:" is a drive, not a directory, and
     * a leading slash is the root. Intermediate separators are restored
     * as '/' because both Windows and POSIX accept it. */
    for (i = 0; buf[i]; i++) {
        if (buf[i] != '/' && buf[i] != '\\')
            continue;
        if (i == 0)
            continue;
        if (i == 2 && buf[1] == ':')
            continue;
        buf[i] = '\0';
        if (mkdir_one(buf) != 0)
            return -1;
        buf[i] = '/';
    }
    return mkdir_one(buf);
}

/* Howard Hinnant's civil_from_days / days_from_civil, public domain. */
static int64_t days_from_civil(int y, unsigned m, unsigned d)
{
    int64_t era;
    unsigned yoe;
    unsigned doy;
    unsigned doe;
    y -= m <= 2;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = (unsigned)(y - era * 400);
    doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

int64_t ymd_hms_unix(int year, int month, int day, int hour, int min, int sec)
{
    int64_t days;
    if (month < 1 || month > 12 || day < 1 || day > 31)
        return -1;
    if (hour < 0 || hour > 23 || min < 0 || min > 59 || sec < 0 || sec > 60)
        return -1;
    days = days_from_civil(year, (unsigned)month, (unsigned)day);
    return days * 86400 + (int64_t)hour * 3600 + (int64_t)min * 60 + sec;
}

static void civil_from_days(int64_t z, int *y, unsigned *m, unsigned *d)
{
    int64_t era;
    unsigned doe;
    unsigned yoe;
    unsigned doy;
    unsigned mp;
    z += 719468;
    era = (z >= 0 ? z : z - 146096) / 146097;
    doe = (unsigned)(z - era * 146097);
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    *y = (int)yoe + (int)era * 400;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp + (mp < 10 ? 3 : -9);
    *y += (*m <= 2);
}

int unix_ymdhms(int64_t t, int *year, int *month, int *day, int *hour, int *min, int *sec)
{
    int64_t days = t / 86400;
    int64_t rem = t % 86400;
    unsigned m, d;
    int y;
    if (rem < 0) {
        rem += 86400;
        days--;
    }
    civil_from_days(days, &y, &m, &d);
    if (year) *year = y;
    if (month) *month = (int)m;
    if (day) *day = (int)d;
    if (hour) *hour = (int)(rem / 3600);
    rem %= 3600;
    if (min) *min = (int)(rem / 60);
    if (sec) *sec = (int)(rem % 60);
    return 0;
}

int format_unix(int64_t t, char *dst, size_t cap)
{
    int y, mo, d, h, mi, s;
    if (t < 0) {
        if (cap < 2)
            return -1;
        dst[0] = '-';
        dst[1] = '\0';
        return 0;
    }
    unix_ymdhms(t, &y, &mo, &d, &h, &mi, &s);
    if (snprintf(dst, cap, "%04d-%02d-%02dT%02d:%02d:%02dZ", y, mo, d, h, mi, s) >= (int)cap)
        return -1;
    return 0;
}

int parse_unix(const char *s, int64_t *out)
{
    int y, mo, d, h, mi, se;
    char tail;
    int64_t t;
    if (!s || !out)
        return -1;
    if (strcmp(s, "-") == 0) {
        *out = -1;
        return 0;
    }
    if (sscanf(s, "%d-%d-%dT%d:%d:%dZ%c", &y, &mo, &d, &h, &mi, &se, &tail) != 6)
        return -1;
    t = ymd_hms_unix(y, mo, d, h, mi, se);
    if (t < 0)
        return -1;
    *out = t;
    return 0;
}

void strlist_free(StrList *list)
{
    size_t i;
    if (!list)
        return;
    for (i = 0; i < list->n; i++)
        free(list->item[i]);
    free(list->item);
    list->item = NULL;
    list->n = 0;
    list->cap = 0;
}

int strlist_add(StrList *list, const char *s)
{
    char *d;
    char **grown;
    if (!list || !s)
        return -1;
    if (list->n == list->cap) {
        size_t cap = list->cap ? list->cap * 2 : 16;
        grown = (char **)realloc(list->item, cap * sizeof(char *));
        if (!grown)
            return -1;
        list->item = grown;
        list->cap = cap;
    }
    d = str_dup(s);
    if (!d)
        return -1;
    list->item[list->n++] = d;
    return 0;
}

static int name_cmp(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

int dir_list(const char *dir, StrList *names)
{
#ifdef _WIN32
    char pattern[LRP_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    if (snprintf(pattern, sizeof pattern, "%s\\*", dir) >= (int)sizeof pattern)
        return -1;
    h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return -1;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        if (strlist_add(names, fd.cFileName) != 0) {
            FindClose(h);
            return -1;
        }
    } while (FindNextFileA(h, &fd));
    /* Any error other than the end of the directory is a partial list.
     * Returning it would hide a base the scan did not reach. */
    if (GetLastError() != ERROR_NO_MORE_FILES) {
        FindClose(h);
        return -1;
    }
    FindClose(h);
#else
    DIR *d;
    struct dirent *de;
    d = opendir(dir);
    if (!d)
        return -1;
    errno = 0;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.')
            continue;
        if (strlist_add(names, de->d_name) != 0) {
            closedir(d);
            return -1;
        }
        errno = 0;
    }
    if (errno != 0) {
        closedir(d);
        return -1;
    }
    closedir(d);
#endif
    if (names->n > 1)
        qsort(names->item, names->n, sizeof(char *), name_cmp);
    return 0;
}
