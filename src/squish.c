#include "squish.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#else
#include <sys/file.h>
#include <sys/types.h>
#include <unistd.h>
#endif

static const char *sq_ext(const char *base, const char *ext, char *dst, size_t cap)
{
    size_t n;
    n = strlen(base);
    if (n >= 4 && str_icmp(base + n - 4, ".sqd") == 0)
        n -= 4;
    else if (n >= 4 && (str_icmp(base + n - 4, ".sqi") == 0 || str_icmp(base + n - 4, ".sql") == 0))
        n -= 4;
    if (n + strlen(ext) + 1 > cap)
        return NULL;
    memcpy(dst, base, n);
    dst[n] = '\0';
    strcat(dst, ext);
    return dst;
}

int sq_open(Sq *sq, const char *base, int user, int exclusive)
{
    char path[LRP_PATH];
    memset(sq, 0, sizeof *sq);
    if (!base || user < 0 || user > 1000000)
        return -1;
    if (strlen(base) >= sizeof sq->base)
        return -1;
    strcpy(sq->base, base);
    sq->user = user;
    sq->exclusive = exclusive;
    if (!sq_ext(base, ".sqi", path, sizeof path))
        return -1;
    /* Absent files are a removed echo, not a failure of this run.
     * The caller skips the area. A file that exists but cannot be
     * opened is still reported. */
    if (!path_exists(path))
        return 2;
    sq->sqi = fopen(path, "rb");
    if (!sq->sqi) {
        log_msg("cannot open %s", path);
        return -1;
    }
    if (!sq_ext(base, ".sqd", path, sizeof path)) {
        sq_close(sq);
        return -1;
    }
    if (!path_exists(path)) {
        sq_close(sq);
        return 2;
    }
#ifdef _WIN32
    {
        HANDLE h = CreateFileA(path, GENERIC_READ,
            exclusive ? 0 : FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        int fd;
        if (h == INVALID_HANDLE_VALUE) {
            DWORD e = GetLastError();
            sq_close(sq);
            if (exclusive && e == ERROR_SHARING_VIOLATION)
                return 1;
            log_msg("cannot open %s", path);
            return -1;
        }
        fd = _open_osfhandle((intptr_t)h, _O_RDONLY);
        if (fd < 0) {
            CloseHandle(h);
            sq_close(sq);
            return -1;
        }
        sq->sqd = _fdopen(fd, "rb");
        if (!sq->sqd) {
            _close(fd);
            sq_close(sq);
            return -1;
        }
        /* _fdopen owns the handle from here. */
    }
#else
    sq->sqd = fopen(path, "rb");
    if (!sq->sqd) {
        sq_close(sq);
        log_msg("cannot open %s", path);
        return -1;
    }
    if (exclusive) {
        /* fileno is POSIX. The build defines _POSIX_C_SOURCE so glibc
         * declares it when the compiler is in strict C11 mode. */
        int fd = fileno(sq->sqd);
        if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
            sq_close(sq);
            return 1;
        }
    }
#endif
    return 0;
}

void sq_close(Sq *sq)
{
    if (!sq)
        return;
    if (sq->sqd)
        fclose(sq->sqd);
    if (sq->sqi)
        fclose(sq->sqi);
    sq->sqd = NULL;
    sq->sqi = NULL;
}

static int sql_path(const Sq *sq, char *dst, size_t cap)
{
    return sq_ext(sq->base, ".sql", dst, cap) ? 0 : -1;
}

int sq_read_lastread(Sq *sq, uint32_t *msgno, int64_t *mtime)
{
    char path[LRP_PATH];
    FILE *fp;
    unsigned char b[4];
    long need;
    if (sql_path(sq, path, sizeof path) != 0)
        return -1;
    /* One slot is 4 bytes. A wild squishuser must not wrap the seek. */
    if (sq->user < 0 || sq->user > 1000000)
        return -1;
    if (mtime && path_mtime(path, mtime) != 0)
        *mtime = 0;
    fp = fopen(path, "rb");
    if (!fp) {
        if (msgno)
            *msgno = 0;
        return 0;
    }
    need = (long)sq->user * 4;
    if (fseek(fp, need, SEEK_SET) != 0) {
        fclose(fp);
        return -1;
    }
    if (fread(b, 1, 4, fp) != 4) {
        fclose(fp);
        if (msgno)
            *msgno = 0;
        return 0;
    }
    fclose(fp);
    if (msgno)
        *msgno = rd_u32(b);
    return 0;
}

int sq_write_lastread(Sq *sq, uint32_t msgno)
{
    char path[LRP_PATH];
    FILE *fp;
    unsigned char b[4];
    long need;
    long have;
    if (sql_path(sq, path, sizeof path) != 0)
        return -1;
    if (sq->user < 0 || sq->user > 1000000)
        return -1;
    fp = fopen(path, "r+b");
    if (!fp) {
        fp = fopen(path, "w+b");
        if (!fp) {
            log_msg("cannot write %s", path);
            return -1;
        }
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }
    have = ftell(fp);
    need = (long)(sq->user + 1) * 4;
    if (have < 0) {
        fclose(fp);
        return -1;
    }
    while (have < need) {
        unsigned char z = 0;
        if (fwrite(&z, 1, 1, fp) != 1) {
            fclose(fp);
            return -1;
        }
        have++;
    }
    wr_u32(b, msgno);
    if (fseek(fp, (long)sq->user * 4, SEEK_SET) != 0 || fwrite(b, 1, 4, fp) != 4) {
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

typedef struct Idx {
    uint32_t offset;
    uint32_t msgno;
    uint32_t hash;
} Idx;

/* Index and data offsets can sit past 2 GiB. long is 32 bits on
 * Windows, so the 64-bit seek is used for both files. */
static int seek_u64(FILE *fp, uint64_t offset)
{
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

static int index_get(Sq *sq, uint32_t i, Idx *out)
{
    unsigned char rec[SQ_IDX_BYTES];
    if (seek_u64(sq->sqi, (uint64_t)i * SQ_IDX_BYTES) != 0)
        return -1;
    if (fread(rec, 1, SQ_IDX_BYTES, sq->sqi) != SQ_IDX_BYTES)
        return -1;
    out->offset = rd_u32(rec);
    out->msgno = rd_u32(rec + 4);
    out->hash = rd_u32(rec + 8);
    return 0;
}

/* How many index records are live messages. hpt reads num_msgs from
 * the 256-byte area header and does not walk the preallocated tail.
 * A header that is missing or still zero is counted by reading records
 * until a free one (offset 0 or message number all-bits-one) or until
 * the number stops increasing. The count is cached on the open base. */
static int index_prepare(Sq *sq)
{
    unsigned char hdr[256];
    uint64_t sz = 0;
    uint64_t slots;
    uint32_t num = 0;
    int trust = 0;
    if (sq->indexed)
        return 0;
    if (file_size64(sq->sqi, &sz) != 0)
        return -1;
    slots = sz / SQ_IDX_BYTES;
    if (slots > 0xffffffffull)
        slots = 0xffffffffull;
    if (seek_u64(sq->sqd, 0) == 0 && fread(hdr, 1, 256, sq->sqd) == 256) {
        unsigned len = rd_u16(hdr);
        unsigned szh = rd_u16(hdr + 130);
        uint32_t msgs = rd_u32(hdr + 4);
        if (len == 256 && szh != 0 && szh != SQ_FRAME_BYTES) {
            log_msg("%s: squish frame header is %u bytes", sq->base, szh);
            return -1;
        }
        if (len == 256 && (szh == 0 || szh == SQ_FRAME_BYTES)
            && msgs > 0 && (uint64_t)msgs <= slots) {
            num = msgs;
            trust = 1;
        }
    }
    if (!trust) {
        uint32_t i;
        uint32_t last = 0;
        num = 0;
        for (i = 0; i < (uint32_t)slots; i++) {
            Idx rec;
            if (index_get(sq, i, &rec) != 0)
                return -1;
            if (rec.offset == 0 || rec.msgno == 0xffffffffu)
                break;
            if (num > 0 && rec.msgno <= last)
                break;
            last = rec.msgno;
            num++;
        }
    }
    sq->nmsg = num;
    sq->indexed = 1;
    return 0;
}

static int month_num(const char *s)
{
    static const char *names = "JanFebMarAprMayJunJulAugSepOctNovDec";
    int i;
    for (i = 0; i < 12; i++) {
        if (tolower((unsigned char)s[0]) == tolower((unsigned char)names[i * 3])
            && tolower((unsigned char)s[1]) == tolower((unsigned char)names[i * 3 + 1])
            && tolower((unsigned char)s[2]) == tolower((unsigned char)names[i * 3 + 2]))
            return i + 1;
    }
    return 0;
}

/* GoldED writes ftsc_date as "DD Mon YY  HH:MM:SS". Two-digit years
 * follow the DOS range the stamp itself uses: 80-99 are 1980-1999. */
static int64_t parse_ftsc(const char *s)
{
    int day, year, hour, min, sec, month;
    char mon[4];
    if (sscanf(s, "%d %3s %d %d:%d:%d", &day, mon, &year, &hour, &min, &sec) != 6)
        return -1;
    month = month_num(mon);
    if (!month)
        return -1;
    if (year < 80)
        year += 2000;
    else if (year < 100)
        year += 1900;
    return ymd_hms_unix(year, month, day, hour, min, sec);
}

/* hpt uses the packed date_written at byte 164. The 20-byte FTSC
 * string at byte 218 is only the tosser's copy, and only when the
 * packed stamp is zero. */
static int64_t date_of_header(const unsigned char *hdr)
{
    uint16_t date;
    uint16_t tim;
    char ftsc[21];
    int64_t t;
    date = rd_u16(hdr + 164);
    tim = rd_u16(hdr + 166);
    if (date != 0 || tim != 0) {
        t = ymd_hms_unix(((date >> 9) & 127) + 1980,
            (date >> 5) & 15,
            date & 31,
            (tim >> 11) & 31,
            (tim >> 5) & 63,
            (tim & 31) * 2);
        if (t >= 0)
            return t;
    }
    memcpy(ftsc, hdr + 218, 20);
    ftsc[20] = '\0';
    return parse_ftsc(ftsc);
}

/*
 * GoldED ScanLine: the keyword runs up to a space or a colon, "MSGID"
 * is matched with case, then a colon is skipped and so is whitespace.
 * The value is the rest of the kludge. The first MSGID wins, which is
 * what GoldED ends up with because it walks the lines from the end
 * and each one overwrites the previous.
 */
static int extract_msgid(const unsigned char *ctl, size_t len, char *out, size_t cap)
{
    size_t i = 0;
    if (cap == 0)
        return -1;
    out[0] = '\0';
    while (i < len) {
        size_t key;
        size_t val;
        size_t n;
        if (ctl[i] != 0x01) {
            i++;
            continue;
        }
        i++;
        key = i;
        while (i < len && ctl[i] != ' ' && ctl[i] != ':' && ctl[i] != 0x01 && ctl[i] != 0)
            i++;
        if ((i - key) == 5 && memcmp(ctl + key, "MSGID", 5) == 0) {
            if (i < len && ctl[i] == ':')
                i++;
            while (i < len && (ctl[i] == ' ' || ctl[i] == '\t'))
                i++;
            val = i;
            while (i < len && ctl[i] != 0x01 && ctl[i] != '\r' && ctl[i] != '\n' && ctl[i] != 0)
                i++;
            while (i > val && (ctl[i - 1] == ' ' || ctl[i - 1] == '\t'))
                i--;
            n = i - val;
            if (n > 200)
                n = 200;
            if (n + 1 > cap)
                n = cap - 1;
            memcpy(out, ctl + val, n);
            out[n] = '\0';
            return out[0] ? 0 : -1;
        }
        while (i < len && ctl[i] != 0x01 && ctl[i] != 0)
            i++;
    }
    return -1;
}

/* 0: header and control were read. 1: this record is not a normal
 * message (free, truncated, or a control length that would read past
 * the frame). -1: the disk read itself failed. The body is never read. */
static int read_frame(FILE *sqd, uint32_t offset, char *msgid, size_t cap, int64_t *date)
{
    unsigned char frame[SQ_FRAME_BYTES];
    unsigned char hdr[SQ_XMSG_BYTES];
    uint32_t id, ctlsize, msglen, type;
    unsigned char *ctl;
    if (offset == 0)
        return 1;
    if (seek_u64(sqd, offset) != 0)
        return -1;
    if (fread(frame, 1, SQ_FRAME_BYTES, sqd) != SQ_FRAME_BYTES)
        return -1;
    id = rd_u32(frame);
    type = rd_u16(frame + 24);
    ctlsize = rd_u32(frame + 20);
    msglen = rd_u32(frame + 16);
    if (id != SQ_FRAME_ID || type != 0)
        return 1;
    if (fread(hdr, 1, SQ_XMSG_BYTES, sqd) != SQ_XMSG_BYTES)
        return -1;
    if (date)
        *date = date_of_header(hdr);
    if (msglen >= SQ_XMSG_BYTES && ctlsize > msglen - SQ_XMSG_BYTES)
        return 1;
    if (ctlsize > 65536)
        return 1;
    ctl = (unsigned char *)malloc(ctlsize + 1);
    if (!ctl)
        return -1;
    if (ctlsize && fread(ctl, 1, ctlsize, sqd) != ctlsize) {
        free(ctl);
        return -1;
    }
    ctl[ctlsize] = 0;
    if (msgid && cap)
        extract_msgid(ctl, ctlsize, msgid, cap);
    free(ctl);
    return 0;
}

int sq_msgid_of(Sq *sq, uint32_t msgno, char *msgid_out, size_t cap, int64_t *date)
{
    uint32_t lo;
    uint32_t hi;
    if (msgid_out && cap)
        msgid_out[0] = '\0';
    if (msgno == 0) {
        if (msgid_out && cap > 1)
            strcpy(msgid_out, "-");
        if (date)
            *date = -1;
        return 0;
    }
    /* The index is sorted by message number, the same order hpt relies
     * on, so the record is found without reading the rest of the file. */
    if (index_prepare(sq) != 0)
        return -1;
    lo = 0;
    hi = sq->nmsg;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        Idx rec;
        int rf;
        if (index_get(sq, mid, &rec) != 0)
            return -1;
        if (rec.msgno == msgno) {
            rf = read_frame(sq->sqd, rec.offset, msgid_out, cap, date);
            if (rf != 0)
                return rf < 0 ? -1 : 1;
            if (!msgid_out || !msgid_out[0])
                return 1;
            return 0;
        }
        if (rec.msgno < msgno)
            lo = mid + 1;
        else
            hi = mid;
    }
    return 1;
}

int sq_find_msgid(Sq *sq, const char *msgid, int64_t target_date, SqHit *hit)
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
    if (index_prepare(sq) != 0)
        return -1;
    /* Index records are read in blocks. Each message contributes its
     * header and control block only, never the body, and never the
     * preallocated tail past num_msgs. */
    while (i < sq->nmsg && !hit->found) {
        unsigned char buf[SQ_IDX_BYTES * 256];
        uint32_t want = sq->nmsg - i;
        uint32_t k;
        if (want > 256)
            want = 256;
        if (seek_u64(sq->sqi, (uint64_t)i * SQ_IDX_BYTES) != 0)
            return -1;
        if (fread(buf, SQ_IDX_BYTES, want, sq->sqi) != want)
            return -1;
        for (k = 0; k < want && !hit->found; k++) {
            uint32_t ofs = rd_u32(buf + k * SQ_IDX_BYTES);
            uint32_t msgno = rd_u32(buf + k * SQ_IDX_BYTES + 4);
            char got[LRP_MSGID];
            int64_t date = -1;
            int rf;
            if (ofs == 0 || msgno == 0xffffffffu)
                continue;
            got[0] = '\0';
            rf = read_frame(sq->sqd, ofs, got, sizeof got, &date);
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
                    snprintf(hit->fallback_msgid, sizeof hit->fallback_msgid, "%s", got[0] ? got : "-");
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
    if (!hit->found && !hit->have_fallback && sq->nmsg > 0) {
        Idx rec;
        char got[LRP_MSGID];
        int64_t date = -1;
        got[0] = '\0';
        if (index_get(sq, sq->nmsg - 1, &rec) == 0
            && read_frame(sq->sqd, rec.offset, got, sizeof got, &date) == 0) {
            hit->have_fallback = 1;
            hit->fallback_msgno = rec.msgno;
            hit->fallback_date = date;
            snprintf(hit->fallback_msgid, sizeof hit->fallback_msgid, "%s", got[0] ? got : "-");
        }
    }
    return 0;
}
