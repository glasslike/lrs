#include "squish.h"

#include <stdio.h>

static void dump(const char *label, const char *base)
{
    Sq sq;
    uint32_t n;
    uint32_t i;
    uint32_t last = 0;
    printf("== %s %s\n", label, base);
    if (sq_open(&sq, base, 0, 0) != 0) {
        printf("open failed\n");
        return;
    }
    if (sq_read_lastread(&sq, &last, NULL) != 0)
        printf("lastread unreadable\n");
    else
        printf("lastread %u\n", last);
    /* nmsg is filled on first msgid lookup. */
    if (sq_msgid_of(&sq, 1, NULL, 0, NULL) != 0 && sq.nmsg == 0)
        printf("no messages\n");
    n = sq.nmsg;
    printf("nmsg %u\n", n);
    {
        FILE *ix = fopen(base[0] ? "" : "", "rb");
        (void)ix;
    }
    for (i = 0; i < n && i < 20; i++) {
        char path[LRP_PATH];
        unsigned char rec[12];
        FILE *ix;
        uint32_t ofs, msg;
        char msgid[LRP_MSGID];
        int64_t date = -1;
        int rc;
        snprintf(path, sizeof path, "%s.sqi", base);
        ix = fopen(path, "rb");
        if (!ix)
            break;
        if (fseek(ix, (long)i * 12, SEEK_SET) != 0 || fread(rec, 1, 12, ix) != 12) {
            fclose(ix);
            break;
        }
        fclose(ix);
        ofs = rd_u32(rec);
        msg = rd_u32(rec + 4);
        rc = sq_msgid_of(&sq, msg, msgid, sizeof msgid, &date);
        printf("  slot %u umsg %u ofs %u rc=%d date=%lld msgid=%s\n",
            i, msg, ofs, rc, (long long)date, rc == 0 ? msgid : "");
    }
    {
        char msgid[LRP_MSGID];
        int64_t date = -1;
        int rc = sq_msgid_of(&sq, last, msgid, sizeof msgid, &date);
        printf("  lastread lookup rc=%d date=%lld msgid=%s\n", rc, (long long)date,
            rc == 0 ? msgid : "");
    }
    sq_close(&sq);
}

int main(void)
{
    dump("node", "Testing/msgbasedir_910/Rabarber.local");
    dump("point", "Testing/msgbasedir_910.1/rabarber.local");
    return 0;
}
