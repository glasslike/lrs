#include "base.h"

#include <string.h>

static int ext_exists(const char *stem, const char *ext)
{
    char path[LRP_PATH];
    if (!stem || !stem[0])
        return 0;
    if (snprintf(path, sizeof path, "%s%s", stem, ext) >= (int)sizeof path)
        return 0;
    return path_exists(path);
}

int base_present(const char *stem)
{
    char dir[LRP_PATH];
    const char *s;
    const char *slash = NULL;
    size_t n;
    int squish;
    int jam;
    if (!stem || !stem[0])
        return -1;
    squish = ext_exists(stem, ".sqd") && ext_exists(stem, ".sqi");
    jam = ext_exists(stem, ".jhr") && ext_exists(stem, ".jdx");
    if (squish || jam)
        return 1;
    for (s = stem; *s; s++) {
        if (*s == '/' || *s == '\\')
            slash = s;
    }
    if (!slash)
        return 0;
    n = (size_t)(slash - stem);
    if (n == 0 || n >= sizeof dir)
        return -1;
    memcpy(dir, stem, n);
    dir[n] = '\0';
    if (!path_exists(dir))
        return -1;
    return 0;
}

int base_open(Base *b, const char *stem, int squishuser, const char *jamuser,
    int exclusive)
{
    int squish;
    int jam;
    memset(b, 0, sizeof *b);
    squish = ext_exists(stem, ".sqd") && ext_exists(stem, ".sqi");
    jam = ext_exists(stem, ".jhr") && ext_exists(stem, ".jdx");
    if (squish && jam) {
        log_msg("%s: both Squish and JAM bases, not used", stem ? stem : "");
        return -1;
    }
    if (jam) {
        int rc = jam_open(&b->jam, stem, jamuser, exclusive);
        if (rc == 0)
            b->kind = BASE_JAM;
        return rc;
    }
    if (squish) {
        int rc = sq_open(&b->sq, stem, squishuser, exclusive);
        if (rc == 0)
            b->kind = BASE_SQUISH;
        return rc;
    }
    return 2;
}

void base_close(Base *b)
{
    if (!b)
        return;
    if (b->kind == BASE_JAM)
        jam_close(&b->jam);
    else if (b->kind == BASE_SQUISH)
        sq_close(&b->sq);
    b->kind = 0;
}

int base_read_lastread(Base *b, uint32_t *msgno, int64_t *mtime)
{
    if (b->kind == BASE_JAM)
        return jam_read_lastread(&b->jam, msgno, mtime);
    return sq_read_lastread(&b->sq, msgno, mtime);
}

int base_write_lastread(Base *b, uint32_t msgno)
{
    if (b->kind == BASE_JAM)
        return jam_write_lastread(&b->jam, msgno);
    return sq_write_lastread(&b->sq, msgno);
}

int base_msgid_of(Base *b, uint32_t msgno, char *msgid_out, size_t cap, int64_t *date)
{
    if (b->kind == BASE_JAM)
        return jam_msgid_of(&b->jam, msgno, msgid_out, cap, date);
    return sq_msgid_of(&b->sq, msgno, msgid_out, cap, date);
}

int base_find_msgid(Base *b, const char *msgid, int64_t target_date, SqHit *hit)
{
    if (b->kind == BASE_JAM)
        return jam_find_msgid(&b->jam, msgid, target_date, hit);
    return sq_find_msgid(&b->sq, msgid, target_date, hit);
}
