#include "jam.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#else
#include <sys/file.h>
#include <unistd.h>
#endif

#define JAM_HDRINFO 1024
#define JAM_HDRFIX  76
#define JAM_SUBHDR  8
#define JAM_IDX     8
#define JAM_LAST    16
#define JAM_MSGID   4
#define JAM_KLUDGE  2000
#define JAM_DELETED 0x80000000u
#define JAM_SUBMAX  65536u

static int seek_u64(FILE *fp, uint64_t offset)
{
    if (!fp)
        return -1;
#ifdef _WIN32
    return _fseeki64(fp, (__int64)offset, SEEK_SET) == 0 ? 0 : -1;
#else
    return fseeko(fp, (off_t)offset, SEEK_SET) == 0 ? 0 : -1;
#endif
}

static int file_size64(FILE *fp, uint64_t *out)
{
#ifdef _WIN32
    __int64 cur;
    __int64 end;
    cur = _ftelli64(fp);
    if (cur < 0 || _fseeki64(fp, 0, SEEK_END) != 0)
        return -1;
    end = _ftelli64(fp);
    if (end < 0 || _fseeki64(fp, cur, SEEK_SET) != 0)
        return -1;
    *out = (uint64_t)end;
#else
    off_t cur;
    off_t end;
    cur = ftello(fp);
    if (cur < 0 || fseeko(fp, 0, SEEK_END) != 0)
        return -1;
    end = ftello(fp);
    if (end < 0 || fseeko(fp, cur, SEEK_SET) != 0)
        return -1;
    *out = (uint64_t)end;
#endif
    return 0;
}

uint32_t jam_user_crc(const char *name)
{
    uint32_t crc = 0xffffffffu;
    const unsigned char *s = (const unsigned char *)(name ? name : "");
    for (; *s; s++) {
        unsigned char c = *s;
        int i;
        if (c >= 'A' && c <= 'Z')
            c = (unsigned char)(c - 'A' + 'a');
        crc ^= c;
        for (i = 0; i < 8; i++)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static int with_ext(char *dst, size_t cap, const char *base, const char *ext)
{
    if (snprintf(dst, cap, "%s%s", base, ext) >= (int)cap)
        return -1;
    return 0;
}

int jam_open(Jam *jam, const char *base, const char *user, int exclusive)
{
    char jhr[LRP_PATH];
    char jdx[LRP_PATH];
    memset(jam, 0, sizeof *jam);
    jam->lr_at = -1;
    if (!base || strlen(base) >= sizeof jam->base)
        return -1;
    if (user && strlen(user) >= sizeof jam->user)
        return -1;
    strcpy(jam->base, base);
    if (user)
        snprintf(jam->user, sizeof jam->user, "%s", user);
    if (with_ext(jhr, sizeof jhr, base, ".jhr") != 0
        || with_ext(jdx, sizeof jdx, base, ".jdx") != 0)
        return -1;
    if (!path_exists(jhr) || !path_exists(jdx))
        return 2;
#ifdef _WIN32
    {
        HANDLE h = CreateFileA(jhr, GENERIC_READ,
            exclusive ? 0 : FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        int fd;
        if (h == INVALID_HANDLE_VALUE) {
            DWORD e = GetLastError();
            if (exclusive && e == ERROR_SHARING_VIOLATION)
                return 1;
            log_msg("cannot open %s", jhr);
            return -1;
        }
        fd = _open_osfhandle((intptr_t)h, _O_RDONLY);
        if (fd < 0) {
            CloseHandle(h);
            return -1;
        }
        jam->jhr = _fdopen(fd, "rb");
        if (!jam->jhr) {
            _close(fd);
            return -1;
        }
    }
#else
    jam->jhr = fopen(jhr, "rb");
    if (!jam->jhr) {
        log_msg("cannot open %s", jhr);
        return -1;
    }
    if (exclusive) {
        int fd = fileno(jam->jhr);
        if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
            jam_close(jam);
            return 1;
        }
    }
#endif
    jam->jdx = fopen(jdx, "rb");
    if (!jam->jdx) {
        log_msg("cannot open %s", jdx);
        jam_close(jam);
        return -1;
    }
    return 0;
}

void jam_close(Jam *jam)
{
    if (!jam)
        return;
    if (jam->jhr)
        fclose(jam->jhr);
    if (jam->jdx)
        fclose(jam->jdx);
    jam->jhr = NULL;
    jam->jdx = NULL;
}

/* Read the 1024-byte area header and remember which .jlr record is
 * ours. A missing .jlr is an area nobody has read yet. Several live
 * records and no jamuser is not a guess: the wrong user's cursor
 * would move. */
static int jam_prepare(Jam *jam)
{
    unsigned char info[JAM_HDRINFO];
    char path[LRP_PATH];
    FILE *lr;
    uint64_t sz = 0;
    uint64_t slots;
    uint32_t want = 0;
    uint32_t alt = 0;
    int64_t at = 0;
    int live = 0;
    int matched = 0;
    if (jam->prepared)
        return 0;
    if (seek_u64(jam->jhr, 0) != 0 || fread(info, 1, JAM_HDRINFO, jam->jhr) != JAM_HDRINFO)
        return -1;
    if (memcmp(info, "JAM", 3) != 0 || info[3] != 0) {
        log_msg("%s: not a JAM header", jam->base);
        return -1;
    }
    jam->base_msg = rd_u32(info + 20);
    if (jam->base_msg == 0)
        jam->base_msg = 1;
    if (file_size64(jam->jdx, &sz) != 0)
        return -1;
    slots = sz / JAM_IDX;
    /* Message numbers are uint32. A longer index cannot be addressed
     * past the last number that still fits after BaseMsgNum. */
    if (slots > 0xffffffffull - jam->base_msg)
        slots = 0xffffffffull - jam->base_msg;
    jam->nidx = (uint32_t)slots;
    if (jam->user[0]) {
        want = jam_user_crc(jam->user);
        alt = ~want;
    }
    if (with_ext(path, sizeof path, jam->base, ".jlr") != 0)
        return -1;
    lr = fopen(path, "rb");
    if (lr) {
        unsigned char rec[JAM_LAST];
        while (fread(rec, 1, JAM_LAST, lr) == JAM_LAST) {
            uint32_t crc = rd_u32(rec);
            uint32_t id = rd_u32(rec + 4);
            if (crc == 0xffffffffu && id == 0xffffffffu) {
                at += JAM_LAST;
                continue;
            }
            live++;
            if (jam->user[0]) {
                if (!matched && (crc == want || crc == alt)) {
                    jam->lr_at = at;
                    jam->lr_high = rd_u32(rec + 12);
                    matched = 1;
                }
            } else if (live == 1) {
                jam->lr_at = at;
                jam->lr_high = rd_u32(rec + 12);
            }
            at += JAM_LAST;
        }
        fclose(lr);
    }
    if (!jam->user[0] && live > 1) {
        log_msg("%s: several JAM users, set jamuser", jam->base);
        return -2;
    }
    jam->lr_live = live;
    jam->prepared = 1;
    return 0;
}

int jam_read_lastread(Jam *jam, uint32_t *msgno, int64_t *mtime)
{
    char path[LRP_PATH];
    FILE *fp;
    unsigned char rec[JAM_LAST];
    {
        int prep = jam_prepare(jam);
        if (prep != 0)
            return prep;
    }
    if (mtime) {
        *mtime = 0;
        if (with_ext(path, sizeof path, jam->base, ".jlr") == 0)
            path_mtime(path, mtime);
    }
    if (jam->lr_at < 0) {
        if (msgno)
            *msgno = 0;
        return 0;
    }
    if (with_ext(path, sizeof path, jam->base, ".jlr") != 0)
        return -1;
    fp = fopen(path, "rb");
    if (!fp)
        return -1;
    if (seek_u64(fp, (uint64_t)jam->lr_at) != 0 || fread(rec, 1, JAM_LAST, fp) != JAM_LAST) {
        fclose(fp);
        return -1;
    }
    fclose(fp);
    jam->lr_high = rd_u32(rec + 12);
    if (msgno)
        *msgno = rd_u32(rec + 8);
    return 0;
}

int jam_write_lastread(Jam *jam, uint32_t msgno)
{
    char path[LRP_PATH];
    FILE *fp;
    unsigned char b[4];
    {
        int prep = jam_prepare(jam);
        if (prep != 0)
            return prep;
    }
    if (with_ext(path, sizeof path, jam->base, ".jlr") != 0)
        return -1;
    if (jam->lr_at < 0) {
        unsigned char rec[JAM_LAST];
        /* No record to update and no name to create one. The caller
         * keeps the cursor and retries. Writing a record with a zero
         * CRC would be a user GoldED never looks up. */
        if (!jam->user[0])
            return -2;
        fp = fopen(path, "ab");
        if (!fp) {
            log_msg("cannot write %s", path);
            return -1;
        }
        {
            uint64_t sz = 0;
            if (file_size64(fp, &sz) != 0 || sz > (uint64_t)INT64_MAX) {
                fclose(fp);
                return -1;
            }
            jam->lr_at = (int64_t)sz;
        }
        /* GoldED looks the user up without the final complement.
         * A record created with the spec's complemented CRC would be
         * invisible to GoldED, which would then append a second one.
         * An existing record is matched either way and is not rewritten. */
        memset(rec, 0, sizeof rec);
        wr_u32(rec, ~jam_user_crc(jam->user));
        wr_u32(rec + 4, 1);
        wr_u32(rec + 8, msgno);
        if (fwrite(rec, 1, JAM_LAST, fp) != JAM_LAST || file_sync(fp) != 0) {
            fclose(fp);
            jam->lr_at = -1;
            return -1;
        }
        if (fclose(fp) != 0) {
            jam->lr_at = -1;
            return -1;
        }
        return 0;
    }
    /* Four bytes in the middle of the record. UserCRC, UserID and
     * HighReadMsg stay as GoldED wrote them. */
    fp = fopen(path, "r+b");
    if (!fp) {
        log_msg("cannot write %s", path);
        return -1;
    }
    wr_u32(b, msgno);
    if (seek_u64(fp, (uint64_t)jam->lr_at + 8) != 0 || fwrite(b, 1, 4, fp) != 4) {
        fclose(fp);
        return -1;
    }
    if (file_sync(fp) != 0) {
        fclose(fp);
        return -1;
    }
    if (fclose(fp) != 0)
        return -1;
    return 0;
}

/* Copy a MSGID value. The first one wins. Longer than 200 characters
 * is the same cut Squish uses, so both formats publish the same
 * identity for the same text. */
static void take_msgid(char *dst, size_t cap, const unsigned char *p, uint32_t n)
{
    uint32_t i;
    if (!dst || cap == 0)
        return;
    while (n && (*p == ' ' || *p == '\t')) {
        p++;
        n--;
    }
    while (n && (p[n - 1] == ' ' || p[n - 1] == '\t'))
        n--;
    if (n > 200)
        n = 200;
    if (n + 1 > cap)
        n = (uint32_t)cap - 1;
    for (i = 0; i < n; i++) {
        if (p[i] == 0)
            break;
        dst[i] = (char)p[i];
    }
    dst[i] = '\0';
}

/* 0 read, 1 not a live message, -1 disk error. The body in .jdt is
 * not touched: MSGID lives in the header subfields. */
static int read_header(Jam *jam, uint32_t offset, char *msgid, size_t cap, int64_t *date)
{
    unsigned char fixed[JAM_HDRFIX];
    unsigned char *sub = NULL;
    uint32_t sublen;
    uint32_t attr;
    uint32_t written;
    uint16_t rev;
    uint32_t pos;
    int have_sub = 0;
    int have_kludge = 0;
    if (offset < JAM_HDRINFO)
        return 1;
    if (seek_u64(jam->jhr, offset) != 0)
        return -1;
    if (fread(fixed, 1, JAM_HDRFIX, jam->jhr) != JAM_HDRFIX)
        return -1;
    if (memcmp(fixed, "JAM", 3) != 0 || fixed[3] != 0)
        return 1;
    rev = rd_u16(fixed + 4);
    if (rev != 1)
        return 1;
    /* Attribute is the 13th field. Deleted messages stay in the index
     * so later numbers do not move, and they are not a cursor target. */
    attr = rd_u32(fixed + 52);
    if (attr & JAM_DELETED)
        return 1;
    written = rd_u32(fixed + 36);
    if (date)
        *date = written ? (int64_t)written : -1;
    sublen = rd_u32(fixed + 8);
    if (sublen > JAM_SUBMAX)
        return 1;
    if (sublen) {
        sub = (unsigned char *)malloc(sublen);
        if (!sub)
            return -1;
        if (fread(sub, 1, sublen, jam->jhr) != sublen) {
            free(sub);
            return -1;
        }
    }
    pos = 0;
    if (msgid && cap)
        msgid[0] = '\0';
    while (sub && pos + JAM_SUBHDR <= sublen) {
        uint16_t loid = rd_u16(sub + pos);
        uint32_t datlen = rd_u32(sub + pos + 4);
        const unsigned char *data;
        pos += JAM_SUBHDR;
        if (datlen > sublen - pos)
            break;
        data = sub + pos;
        /* Subfield 4 is the MSGID. A kludge line is only a fallback
         * for a tosser that never wrote that subfield, and the first
         * of either kind wins. */
        if (!have_sub && loid == JAM_MSGID && datlen && msgid && cap) {
            take_msgid(msgid, cap, data, datlen);
            if (msgid[0])
                have_sub = 1;
        } else if (!have_sub && !have_kludge && loid == JAM_KLUDGE && datlen > 6
            && msgid && cap
            && (memcmp(data, "MSGID:", 6) == 0 || memcmp(data, "MSGID ", 6) == 0)) {
            take_msgid(msgid, cap, data + 6, datlen - 6);
            if (msgid[0])
                have_kludge = 1;
        }
        pos += datlen;
    }
    free(sub);
    return 0;
}

static int index_at(Jam *jam, uint32_t i, uint32_t *crc, uint32_t *offset)
{
    unsigned char rec[JAM_IDX];
    if (seek_u64(jam->jdx, (uint64_t)i * JAM_IDX) != 0)
        return -1;
    if (fread(rec, 1, JAM_IDX, jam->jdx) != JAM_IDX)
        return -1;
    *crc = rd_u32(rec);
    *offset = rd_u32(rec + 4);
    return 0;
}

int jam_msgid_of(Jam *jam, uint32_t msgno, char *msgid_out, size_t cap, int64_t *date)
{
    uint32_t crc;
    uint32_t offset;
    uint32_t slot;
    int rf;
    if (msgid_out && cap)
        msgid_out[0] = '\0';
    if (msgno == 0) {
        if (msgid_out && cap > 1)
            strcpy(msgid_out, "-");
        if (date)
            *date = -1;
        return 0;
    }
    {
        int prep = jam_prepare(jam);
        if (prep != 0)
            return prep;
    }
    if (msgno < jam->base_msg)
        return 1;
    slot = msgno - jam->base_msg;
    if (slot >= jam->nidx)
        return 1;
    if (index_at(jam, slot, &crc, &offset) != 0)
        return -1;
    /* A deleted slot has no header. Seeking to 0xffffffff would look
     * like a disk error and abort the whole lookup. */
    if (offset == 0xffffffffu)
        return 1;
    rf = read_header(jam, offset, msgid_out, cap, date);
    if (rf != 0)
        return rf < 0 ? -1 : 1;
    if (!msgid_out || !msgid_out[0])
        return 1;
    return 0;
}

int jam_find_msgid(Jam *jam, const char *msgid, int64_t target_date, SqHit *hit)
{
    uint32_t i = 0;
    memset(hit, 0, sizeof *hit);
    hit->date = -1;
    hit->oldest = -1;
    hit->fallback_date = -1;
    if (!msgid || !msgid[0] || strcmp(msgid, "-") == 0) {
        hit->found = 1;
        hit->msgno = 0;
        return 0;
    }
    {
        int prep = jam_prepare(jam);
        if (prep != 0)
            return prep;
    }
    while (i < jam->nidx && !hit->found) {
        unsigned char buf[JAM_IDX * 256];
        uint32_t want = jam->nidx - i;
        uint32_t k;
        if (want > 256)
            want = 256;
        if (seek_u64(jam->jdx, (uint64_t)i * JAM_IDX) != 0)
            return -1;
        if (fread(buf, JAM_IDX, want, jam->jdx) != want)
            return -1;
        for (k = 0; k < want && !hit->found; k++) {
            uint32_t offset = rd_u32(buf + k * JAM_IDX + 4);
            uint32_t msgno;
            char got[LRP_MSGID];
            int64_t date = -1;
            int rf;
            if (offset == 0xffffffffu)
                continue;
            if (jam->base_msg > 0xffffffffu - (i + k))
                continue;
            msgno = jam->base_msg + i + k;
            got[0] = '\0';
            rf = read_header(jam, offset, got, sizeof got, &date);
            if (rf < 0)
                return -1;
            if (rf != 0)
                continue;
            if (date >= 0 && (hit->oldest < 0 || date < hit->oldest))
                hit->oldest = date;
            if (!hit->have_fallback) {
                if (target_date < 0 || (date >= 0 && date >= target_date)) {
                    hit->have_fallback = 1;
                    hit->fallback_msgno = msgno;
                    hit->fallback_date = date;
                    snprintf(hit->fallback_msgid, sizeof hit->fallback_msgid, "%s",
                        got[0] ? got : "-");
                }
            }
            if (got[0] && strcmp(got, msgid) == 0) {
                hit->found = 1;
                hit->msgno = msgno;
                hit->date = date;
            }
        }
        i += want;
    }
    if (!hit->found && !hit->have_fallback && jam->nidx > 0) {
        uint32_t crc;
        uint32_t offset;
        char got[LRP_MSGID];
        int64_t date = -1;
        got[0] = '\0';
        if (index_at(jam, jam->nidx - 1, &crc, &offset) == 0
            && offset != 0xffffffffu
            && read_header(jam, offset, got, sizeof got, &date) == 0) {
            hit->have_fallback = 1;
            hit->fallback_msgno = jam->base_msg + jam->nidx - 1;
            hit->fallback_date = date;
            snprintf(hit->fallback_msgid, sizeof hit->fallback_msgid, "%s",
                got[0] ? got : "-");
        }
    }
    return 0;
}
