#include "base.h"
#include "lrpfile.h"
#include "state.h"
#include "sync.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define mkdir_one(p) _mkdir(p)
#else
#include <sys/stat.h>
#define mkdir_one(p) mkdir((p), 0755)
#endif

static int g_fail;

static void expect(int cond, const char *what)
{
    if (!cond) {
        fprintf(stderr, "selftest fail: %s\n", what);
        g_fail++;
    }
}

static void put_u16(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)(v & 0xff);
    p[1] = (unsigned char)((v >> 8) & 0xff);
}

static int write_bytes(const char *path, const unsigned char *b, size_t n)
{
    FILE *fp = fopen(path, "wb");
    if (!fp)
        return -1;
    if (fwrite(b, 1, n, fp) != n) {
        fclose(fp);
        return -1;
    }
    return fclose(fp);
}

/* One Squish message whose MSGID is not an FTN address, plus a second
 * message with two MSGID kludges so the first one is the identity. */
static int build_base(const char *dir)
{
    unsigned char sqd[2048];
    unsigned char sqi[24];
    unsigned char sql[8];
    const char *ctl1 = "\x01MSGID: <1187509996@ddt.demos.su> 93d75b8f\x01PID: lrs";
    const char *ctl2 = "\x01MSGID: FIRST 00000001\x01MSGID: SECOND 00000002";
    const char *text = "Hi";
    size_t c1 = strlen(ctl1) + 1;
    size_t c2 = strlen(ctl2) + 1;
    size_t tx = strlen(text) + 1;
    uint32_t off1 = 256;
    uint32_t tot1 = (uint32_t)(SQ_XMSG_BYTES + c1 + tx);
    uint32_t off2;
    uint32_t tot2;
    char path[LRP_PATH];
    unsigned char *frame;
    unsigned char *hdr;
    memset(sqd, 0, sizeof sqd);
    memset(sqi, 0, sizeof sqi);
    memset(sql, 0, sizeof sql);
    /* Area header the way hpt writes it: 256 bytes, two live messages,
     * frame header of 28. The index tail past num_msgs is not a message. */
    put_u16(sqd + 0, 256);
    wr_u32(sqd + 4, 2);
    put_u16(sqd + 130, 28);
    frame = sqd + off1;
    wr_u32(frame, SQ_FRAME_ID);
    wr_u32(frame + 12, tot1);
    wr_u32(frame + 16, tot1);
    wr_u32(frame + 20, (uint32_t)c1);
    put_u16(frame + 24, 0);
    hdr = frame + SQ_FRAME_BYTES;
    memcpy(hdr + 218, "19 Aug 97  12:00:00", 18);
    memcpy(hdr + SQ_XMSG_BYTES, ctl1, c1);
    memcpy(hdr + SQ_XMSG_BYTES + c1, text, tx);
    off2 = off1 + SQ_FRAME_BYTES + tot1;
    tot2 = (uint32_t)(SQ_XMSG_BYTES + c2 + tx);
    if (off2 + SQ_FRAME_BYTES + tot2 > sizeof sqd)
        return -1;
    frame = sqd + off2;
    wr_u32(frame, SQ_FRAME_ID);
    wr_u32(frame + 12, tot2);
    wr_u32(frame + 16, tot2);
    wr_u32(frame + 20, (uint32_t)c2);
    hdr = frame + SQ_FRAME_BYTES;
    memcpy(hdr + SQ_XMSG_BYTES, ctl2, c2);
    memcpy(hdr + SQ_XMSG_BYTES + c2, text, tx);
    wr_u32(sqi + 0, off1);
    wr_u32(sqi + 4, 1);
    wr_u32(sqi + 12, off2);
    wr_u32(sqi + 16, 2);
    wr_u32(sql, 5);
    if (path_join(path, sizeof path, dir, "echo.sqd") != 0)
        return -1;
    if (write_bytes(path, sqd, off2 + SQ_FRAME_BYTES + tot2) != 0)
        return -1;
    if (path_join(path, sizeof path, dir, "echo.sqi") != 0)
        return -1;
    if (write_bytes(path, sqi, 24) != 0)
        return -1;
    if (path_join(path, sizeof path, dir, "echo.sql") != 0)
        return -1;
    if (write_bytes(path, sql, 4) != 0)
        return -1;
    return 0;
}

/* qico BSO file stem inside the configured outbound. 203 is 0xCB and
 * 910 is 0x38E, so 2:203/910 is outbound/00cb038e.flo. A point adds
 * .pnt. The zone is not a directory suffix: 1:2/3 still lands in the
 * same outbound the config named. */
static void test_bso(void)
{
    char flo[LRP_PATH];
    char bsy[LRP_PATH];
    expect(bso_flo_bsy("D:/fido/outbound", "2:203/910",
            flo, sizeof flo, bsy, sizeof bsy) == 0, "bso node");
    expect(strcmp(flo, "D:/fido/outbound/00cb038e.flo") == 0, "node flo");
    expect(strcmp(bsy, "D:/fido/outbound/00cb038e.bsy") == 0, "node bsy");
    expect(bso_flo_bsy("D:/fido/outbound", "2:203/910.1",
            flo, sizeof flo, bsy, sizeof bsy) == 0, "bso point");
    expect(strcmp(flo, "D:/fido/outbound/00cb038e.pnt/00000001.flo") == 0, "point flo");
    expect(strcmp(bsy, "D:/fido/outbound/00cb038e.pnt/00000001.bsy") == 0, "point bsy");
    expect(bso_flo_bsy("D:/fido/outbound/", "1:2/3",
            flo, sizeof flo, bsy, sizeof bsy) == 0, "zone stays in outbound");
    expect(strcmp(flo, "D:/fido/outbound/00020003.flo") == 0, "zone flo");
    expect(strcmp(bsy, "D:/fido/outbound/00020003.bsy") == 0, "zone bsy");
}

static void test_decide(void)
{
    expect(next_snapshot_seq(0) == 1, "first snapshot number is 1");
    expect(next_snapshot_seq(7) == 8, "snapshot number grows");
    expect(next_snapshot_seq(9999999u) == 1, "snapshot number restarts after 9999999");
    expect(next_snapshot_seq(10000000u) == 1, "snapshot number past the end restarts");
    expect(decide_cursor(5, 3, 3, 0, 10, 20, "2:203/910", "2:203/910.1", 1) == DEC_IGNORE,
        "stale gen is ignored");
    expect(decide_cursor(0, 0, 2, 0, 0, 10, "2:203/910.1", "2:203/910", 0) == DEC_ACCEPT,
        "first snapshot initializes an area we have not published");
    expect(decide_cursor(5, 1, 2, 5, 10, 1, "2:203/910", "2:203/910.1", 1) == DEC_ACCEPT,
        "peer saw our latest gen");
    expect(decide_cursor(5, 0, 1, 0, 10, 40, "2:203/910", "2:203/910.1", 1) == DEC_KEEP,
        "server keeps its cursor against an unseen open");
    expect(decide_cursor(5, 0, 1, 0, 40, 10, "2:203/910", "2:203/910.1", 1) == DEC_KEEP,
        "server keeps a newer cursor against an older unseen open");
    expect(decide_cursor(5, 0, 1, 0, 40, 10, "2:203/910.1", "2:203/910", 0) == DEC_ACCEPT,
        "downlink takes the server even when the local open is newer");
    expect(decide_cursor(1, 1, 2, 0, 40, 10, "2:203/910.1", "2:203/910", 0) == DEC_KEEP,
        "point already in sync keeps a newer cursor the server has not seen");
    expect(decide_cursor(1, 1, 2, 0, 10, 40, "2:203/910.1", "2:203/910", 0) == DEC_ACCEPT,
        "point already in sync takes a newer unseen server cursor");
    expect(decide_cursor(5, 2, 4, 2, 10, 30, "2:203/910", "2:203/910.1", 1) == DEC_ACCEPT,
        "later remote session wins a conflict");
    expect(decide_cursor(5, 2, 4, 2, 40, 30, "2:203/910", "2:203/910.1", 0) == DEC_KEEP,
        "later local session keeps the cursor");
    expect(decide_cursor(5, 2, 4, 2, 10, 10, "2:203/910", "2:203/910.1", 0) == DEC_ACCEPT,
        "equal time, greater address wins");
    expect(decide_cursor(5, 2, 4, 2, 10, 10, "2:203/910.1", "2:203/910", 1) == DEC_KEEP,
        "equal time, smaller address loses");
}

static void test_lrp_text(void)
{
    LrpFile in, out;
    LrpArea a;
    char *text;
    memset(&in, 0, sizeof in);
    memset(&a, 0, sizeof a);
    snprintf(in.from, sizeof in.from, "%s", "2:203/910.1");
    snprintf(a.tag, sizeof a.tag, "%s", "RU.GOLDED");
    snprintf(a.msgid, sizeof a.msgid, "%s", "<1187509996@ddt.demos.su> 93d75b8f");
    a.date = ymd_hms_unix(1997, 8, 19, 12, 0, 0);
    a.gen = 14;
    a.seen = 9;
    a.updated = ymd_hms_unix(2026, 10, 30, 17, 40, 12);
    expect(lrp_file_add(&in, &a) == 0, "add area");
    text = lrp_file_format(&in);
    expect(text != NULL, "format");
    expect(lrp_file_parse(text, &out) == 0, "parse");
    expect(strcmp(out.from, "2:203/910.1") == 0, "from");
    expect(out.n == 1, "one area");
    expect(strcmp(out.area[0].msgid, "<1187509996@ddt.demos.su> 93d75b8f") == 0, "opaque msgid");
    expect(out.area[0].gen == 14 && out.area[0].seen == 9, "gens");
    expect(out.area[0].date == a.date, "message date");
    expect(out.area[0].request == 0, "cursor is not a request");
    free(text);
    lrp_file_free(&out);
    in.area[0].request = 1;
    in.area[0].gen = 0;
    text = lrp_file_format(&in);
    expect(text && strstr(text, "\"lrp\": 2") != NULL, "json snapshot");
    expect(text && strstr(text, "\"request\":1") != NULL, "request field");
    expect(lrp_file_parse(text, &out) == 0 && out.n == 1 && out.area[0].request == 1,
        "request roundtrip");
    free(text);
    lrp_file_free(&out);
    /* A quote, a backslash and a newline must survive as one msgid. */
    snprintf(in.area[0].msgid, sizeof in.area[0].msgid, "say \"hi\" \\ next\nline");
    text = lrp_file_format(&in);
    expect(lrp_file_parse(text, &out) == 0 && out.n == 1
        && strcmp(out.area[0].msgid, "say \"hi\" \\ next\nline") == 0,
        "msgid escapes roundtrip");
    free(text);
    lrp_file_free(&in);
    lrp_file_free(&out);
    expect(lrp_file_parse(
            "LRP 2\nfrom 2:203/910\necho RU.GOLDED gen=1 seen=0 date=- updated=- msgid=abc def\n",
            &out) != 0,
        "line snapshot is not a snapshot");
    lrp_file_free(&out);
}

/* The state file is the same JSON string rules as a snapshot. A quote
 * and a newline in a MSGID must come back as the same bytes. */
static int keep_golded(const char *tag, void *ctx)
{
    (void)ctx;
    return strcmp(tag, "RU.GOLDED") == 0;
}

static void test_state(void)
{
    State st;
    State back;
    StArea *a;
    char *text;
    const char *path = "lrs-selftest/state.json";
    mkdir_one("lrs-selftest");
    memset(&st, 0, sizeof st);
    memset(&back, 0, sizeof back);
    st.sendseq = 7;
    snprintf(st.queued, sizeof st.queued, "%s", "c0000007.lrs");
    a = state_area(&st, "RU.GOLDED");
    expect(a != NULL, "state area");
    if (!a)
        return;
    snprintf(a->msgid, sizeof a->msgid, "say \"hi\" \\ next\nline");
    a->date = ymd_hms_unix(1997, 8, 19, 12, 0, 0);
    a->msgno = 3;
    a->gen = 4;
    a->seen = 2;
    a->peergen = 2;
    a->updated = ymd_hms_unix(2026, 10, 30, 17, 40, 12);
    a->pending = 1;
    snprintf(a->pend_msgid, sizeof a->pend_msgid, "%s", "pend \"x\"");
    a->pend_date = a->date;
    a->pend_gen = 9;
    a->pend_seen = 8;
    a->pend_updated = a->updated;
    snprintf(a->pend_from, sizeof a->pend_from, "%s", "2:203/910");
    a->hold = 1;
    a->ask = 1;
    a->asked = 1;
    a->reply = 1;
    expect(state_save(path, &st) == 0, "save state");
    text = read_all(path, NULL);
    expect(text && strstr(text, "\"lrs\": 1") != NULL, "state is json");
    expect(text && strstr(text, "sendseq ") == NULL, "state is not the line form");
    free(text);
    expect(state_load(path, &back) == 0, "load state");
    expect(back.sendseq == 7, "sendseq");
    expect(strcmp(back.queued, "c0000007.lrs") == 0, "queued name");
    expect(back.n == 1, "one stored area");
    if (back.n == 1) {
        StArea *b = &back.area[0];
        expect(strcmp(b->tag, "RU.GOLDED") == 0, "state tag");
        expect(strcmp(b->msgid, "say \"hi\" \\ next\nline") == 0, "state msgid escapes");
        expect(b->date == a->date && b->updated == a->updated, "state times");
        expect(b->msgno == 3 && b->gen == 4 && b->seen == 2 && b->peergen == 2, "state counters");
        expect(b->pending == 1 && strcmp(b->pend_msgid, "pend \"x\"") == 0, "pending msgid");
        expect(b->pend_gen == 9 && b->pend_seen == 8 && strcmp(b->pend_from, "2:203/910") == 0,
            "pending cursor");
        expect(b->hold == 1 && b->ask == 1 && b->asked == 1 && b->reply == 1, "state flags");
    }
    state_free(&st);
    state_free(&back);
    expect(state_load("lrs-selftest/no-such-state", &back) == 0 && back.n == 0,
        "missing state is empty");
    state_free(&back);
    expect(write_atomic("lrs-selftest/old.state", "sendseq 1\nqueued c0000001.lrs\n") == 0,
        "write a line state");
    expect(state_load("lrs-selftest/old.state", &back) != 0, "line state is not read");
    state_free(&back);
    memset(&st, 0, sizeof st);
    expect(state_area(&st, "RU.GOLDED") != NULL && state_area(&st, "gone") != NULL,
        "two state echoes");
    state_prune(&st, keep_golded, NULL);
    expect(st.n == 1 && strcmp(st.area[0].tag, "RU.GOLDED") == 0,
        "state drops an echo that left the config");
    state_free(&st);
}

static void test_squish(void)
{
    char dir[LRP_PATH];
    char base[LRP_PATH];
    Sq sq;
    char msgid[LRP_MSGID];
    int64_t date = -1;
    uint32_t msgno = 0;
    SqHit hit;
    snprintf(dir, sizeof dir, "lrs-selftest");
    mkdir_one(dir);
    expect(build_base(dir) == 0, "build base");
    expect(path_join(base, sizeof base, dir, "echo") == 0, "base path");
    expect(sq_open(&sq, base, 0, 0) == 0, "open");
    expect(sq_read_lastread(&sq, &msgno, NULL) == 0, "read sql");
    expect(msgno == 5, "sql slot is 5");
    expect(sq_msgid_of(&sq, 1, msgid, sizeof msgid, &date) == 0, "msgid of 1");
    expect(strcmp(msgid, "<1187509996@ddt.demos.su> 93d75b8f") == 0, "angle msgid");
    expect(date > 0, "ftsc date");
    expect(sq_msgid_of(&sq, 2, msgid, sizeof msgid, NULL) == 0, "msgid of 2");
    expect(strcmp(msgid, "FIRST 00000001") == 0, "first MSGID wins");
    memset(&hit, 0, sizeof hit);
    expect(sq_find_msgid(&sq, "<1187509996@ddt.demos.su> 93d75b8f", date, &hit) == 0, "find");
    expect(hit.found && hit.msgno == 1, "found msg 1");
    expect(sq_write_lastread(&sq, 2) == 0, "write sql");
    sq_close(&sq);
    expect(sq_open(&sq, base, 0, 0) == 0, "reopen");
    expect(sq_read_lastread(&sq, &msgno, NULL) == 0 && msgno == 2, "sql updated");
    sq_close(&sq);
    expect(sq_open(&sq, base, 1, 0) == 0, "user 1");
    expect(sq_write_lastread(&sq, 9) == 0, "write user 1");
    sq_close(&sq);
    expect(sq_open(&sq, base, 0, 0) == 0, "user 0 again");
    expect(sq_read_lastread(&sq, &msgno, NULL) == 0 && msgno == 2, "user 0 unchanged");
    sq_close(&sq);
}

/* One JAM message, a deleted one, and an index hole. Lastread is stored
 * with GoldED's CRC (no final complement). Writing the cursor must
 * change only LastReadMsg. */
static int build_jam(const char *dir)
{
    unsigned char hdrinfo[1024];
    unsigned char msg[256];
    unsigned char idx[24];
    unsigned char last[16];
    char path[LRP_PATH];
    const char *kludge = "MSGID: NOT-THIS";
    const char *msgid = "jam-one";
    uint32_t sublen;
    uint32_t msg2;
    size_t n = 0;
    memset(hdrinfo, 0, sizeof hdrinfo);
    memset(msg, 0, sizeof msg);
    memset(idx, 0, sizeof idx);
    memcpy(hdrinfo, "JAM", 4);
    wr_u32(hdrinfo + 20, 1);
    memcpy(msg, "JAM", 4);
    put_u16(msg + 4, 1);
    sublen = 8 + (uint32_t)strlen(kludge) + 8 + (uint32_t)strlen(msgid);
    wr_u32(msg + 8, sublen);
    wr_u32(msg + 36, 800000000u);
    put_u16(msg + 76, 2000);
    wr_u32(msg + 80, (uint32_t)strlen(kludge));
    memcpy(msg + 84, kludge, strlen(kludge));
    n = 84 + strlen(kludge);
    put_u16(msg + n, 4);
    wr_u32(msg + n + 4, (uint32_t)strlen(msgid));
    memcpy(msg + n + 8, msgid, strlen(msgid));
    n += 8 + strlen(msgid);
    msg2 = (uint32_t)n;
    if (76 + n > sizeof msg)
        return -1;
    memcpy(msg + n, "JAM", 4);
    put_u16(msg + n + 4, 1);
    wr_u32(msg + n + 52, 0x80000000u);
    n += 76;
    wr_u32(idx + 0, 0);
    wr_u32(idx + 4, 1024);
    wr_u32(idx + 8, 0);
    wr_u32(idx + 12, 1024 + msg2);
    wr_u32(idx + 16, 0xffffffffu);
    wr_u32(idx + 20, 0xffffffffu);
    wr_u32(last + 0, ~jam_user_crc("Sysop"));
    wr_u32(last + 4, 7);
    wr_u32(last + 8, 1);
    wr_u32(last + 12, 99);
    if (path_join(path, sizeof path, dir, "jamecho.jhr") != 0)
        return -1;
    if (write_bytes(path, hdrinfo, 1024) != 0)
        return -1;
    {
        FILE *fp = fopen(path, "ab");
        if (!fp || fwrite(msg, 1, n, fp) != n) {
            if (fp)
                fclose(fp);
            return -1;
        }
        if (fclose(fp) != 0)
            return -1;
    }
    if (path_join(path, sizeof path, dir, "jamecho.jdx") != 0)
        return -1;
    if (write_bytes(path, idx, 24) != 0)
        return -1;
    if (path_join(path, sizeof path, dir, "jamecho.jlr") != 0)
        return -1;
    if (write_bytes(path, last, 16) != 0)
        return -1;
    return 0;
}

static void test_jam(void)
{
    char dir[LRP_PATH];
    char base[LRP_PATH];
    char path[LRP_PATH];
    Jam jam;
    Base b;
    unsigned char rec[16];
    char msgid[LRP_MSGID];
    int64_t date = -1;
    uint32_t msgno = 0;
    SqHit hit;
    snprintf(dir, sizeof dir, "lrs-selftest");
    mkdir_one(dir);
    expect(jam_user_crc("123456789") == 0xCBF43926u, "jam crc of 123456789");
    expect(build_jam(dir) == 0, "build jam");
    expect(path_join(base, sizeof base, dir, "jamecho") == 0, "jam path");
    expect(base_present(base) == 1, "jam base is present without squish files");
    expect(jam_open(&jam, base, "Sysop", 0) == 0, "open jam");
    expect(jam_read_lastread(&jam, &msgno, NULL) == 0 && msgno == 1, "golded crc matches");
    expect(jam_msgid_of(&jam, 1, msgid, sizeof msgid, &date) == 0, "jam msgid");
    expect(strcmp(msgid, "jam-one") == 0, "subfield msgid beats the kludge");
    expect(date == 800000000, "jam date");
    expect(jam_msgid_of(&jam, 2, msgid, sizeof msgid, NULL) != 0, "deleted is not a message");
    expect(jam_msgid_of(&jam, 3, msgid, sizeof msgid, NULL) != 0, "index hole is not a message");
    memset(&hit, 0, sizeof hit);
    expect(jam_find_msgid(&jam, "jam-one", date, &hit) == 0 && hit.found && hit.msgno == 1,
        "find jam msgid");
    expect(jam_write_lastread(&jam, 4) == 0, "write jam lastread");
    jam_close(&jam);
    expect(path_join(path, sizeof path, dir, "jamecho.jlr") == 0, "jlr path");
    {
        FILE *fp = fopen(path, "rb");
        expect(fp && fread(rec, 1, 16, fp) == 16, "read jlr back");
        if (fp)
            fclose(fp);
    }
    expect(rd_u32(rec) == ~jam_user_crc("Sysop"), "usercrc unchanged");
    expect(rd_u32(rec + 4) == 7, "userid unchanged");
    expect(rd_u32(rec + 8) == 4, "lastread updated");
    expect(rd_u32(rec + 12) == 99, "highread unchanged");
    expect(jam_open(&jam, base, "", 0) == 0, "open jam without a user");
    expect(jam_read_lastread(&jam, &msgno, NULL) == 0 && msgno == 4, "single record is used");
    jam_close(&jam);
    expect(base_open(&b, base, 0, "Sysop", 0) == 0 && b.kind == BASE_JAM, "dispatch jam");
    base_close(&b);
    expect(path_join(base, sizeof base, dir, "echo") == 0, "squish still there");
    expect(base_open(&b, base, 0, "", 0) == 0 && b.kind == BASE_SQUISH, "dispatch squish");
    base_close(&b);
    expect(path_join(base, sizeof base, dir, "both") == 0, "both path");
    expect(path_join(path, sizeof path, dir, "both.sqd") == 0, "both sqd");
    expect(write_bytes(path, (const unsigned char *)"x", 1) == 0, "write both sqd");
    expect(path_join(path, sizeof path, dir, "both.sqi") == 0, "both sqi");
    expect(write_bytes(path, (const unsigned char *)"x", 1) == 0, "write both sqi");
    expect(path_join(path, sizeof path, dir, "both.jhr") == 0, "both jhr");
    expect(write_bytes(path, (const unsigned char *)"x", 1) == 0, "write both jhr");
    expect(path_join(path, sizeof path, dir, "both.jdx") == 0, "both jdx");
    expect(write_bytes(path, (const unsigned char *)"x", 1) == 0, "write both jdx");
    expect(base_open(&b, base, 0, "Sysop", 0) == -1, "both formats are refused");
    wr_u32(rec, jam_user_crc("Other"));
    wr_u32(rec + 4, 2);
    wr_u32(rec + 8, 3);
    wr_u32(rec + 12, 3);
    expect(path_join(path, sizeof path, dir, "jamecho.jlr") == 0, "jlr again");
    {
        FILE *fp = fopen(path, "ab");
        expect(fp && fwrite(rec, 1, 16, fp) == 16, "second user");
        if (fp)
            fclose(fp);
    }
    expect(path_join(base, sizeof base, dir, "jamecho") == 0, "back to the jam base");
    expect(jam_open(&jam, base, "", 0) == 0, "open two users");
    expect(jam_read_lastread(&jam, &msgno, NULL) == -2, "two users are not guessed");
    jam_close(&jam);
    expect(jam_open(&jam, base, "Other", 0) == 0, "open the second user");
    expect(jam_read_lastread(&jam, &msgno, NULL) == 0 && msgno == 3, "named user");
    jam_close(&jam);
    expect(path_join(base, sizeof base, dir, "jamnew") == 0, "new jam path");
    expect(path_join(path, sizeof path, dir, "jamnew.jhr") == 0, "new jhr");
    expect(write_bytes(path, (const unsigned char *)"JAM", 4) == 0, "stub jhr");
    {
        unsigned char info[1024];
        FILE *fp;
        memset(info, 0, sizeof info);
        memcpy(info, "JAM", 4);
        wr_u32(info + 20, 1);
        fp = fopen(path, "wb");
        expect(fp && fwrite(info, 1, 1024, fp) == 1024, "new jam header");
        if (fp)
            fclose(fp);
    }
    expect(path_join(path, sizeof path, dir, "jamnew.jdx") == 0, "new jdx");
    expect(write_bytes(path, (const unsigned char *)"", 0) == 0, "empty index");
    expect(path_join(path, sizeof path, dir, "jamnew.jlr") == 0, "clear a previous jlr");
    remove(path);
    expect(jam_open(&jam, base, "", 0) == 0, "open unread without a user");
    expect(jam_write_lastread(&jam, 1) == -2, "missing user defers the write");
    jam_close(&jam);
    expect(jam_open(&jam, base, "Sysop", 0) == 0, "open jam with no lastread");
    expect(jam_read_lastread(&jam, &msgno, NULL) == 0 && msgno == 0, "unread area");
    expect(jam_write_lastread(&jam, 2) == 0, "create lastread");
    jam_close(&jam);
    expect(path_join(path, sizeof path, dir, "jamnew.jlr") == 0, "created jlr");
    {
        FILE *fp = fopen(path, "rb");
        expect(fp && fread(rec, 1, 16, fp) == 16, "read created jlr");
        if (fp)
            fclose(fp);
    }
    expect(rd_u32(rec) == ~jam_user_crc("Sysop"), "new record uses golded crc");
    expect(rd_u32(rec + 8) == 2 && rd_u32(rec + 12) == 0, "new lastread, highread stays 0");
}

static void test_log(void)
{
    char path[LRP_PATH];
    char *text;
    snprintf(path, sizeof path, "lrs-selftest/lrs.log");
    remove(path);
    log_open(path);
    log_msg("log probe");
    log_close();
    text = read_all(path, NULL);
    expect(text && strstr(text, "log probe") != NULL, "log file received the line");
    free(text);
}

static void test_areas(void)
{
    Config cfg;
    char *text;
    FILE *fp;
    const char *conf = "lrs-selftest/areas.conf";
    mkdir_one("lrs-selftest");
    mkdir_one("lrs-selftest/echos");
    fp = fopen("lrs-selftest/echos/ru.qico.sqd", "wb");
    expect(fp != NULL, "create ru.qico.sqd");
    if (fp)
        fclose(fp);
    fp = fopen("lrs-selftest/echos/extra.sqd", "wb");
    expect(fp != NULL, "create extra.sqd");
    if (fp)
        fclose(fp);
    fp = fopen("lrs-selftest/echos/more.sqd", "wb");
    expect(fp != NULL, "create more.sqd");
    if (fp)
        fclose(fp);
    fp = fopen(conf, "wb");
    expect(fp != NULL, "create areas.conf");
    if (!fp)
        return;
    fputs("address 2:203/910\n"
          "peer 2:203/910.1\n"
          "inbound lrs-selftest\n"
          "outbound lrs-selftest\n"
          "state lrs-selftest/x.state\n"
          "echodir lrs-selftest/echos\n"
          "echoarea ru.qico\n"
          "echoarea OTHER lrs-selftest/explicit\n", fp);
    fclose(fp);
    expect(config_load(conf, &cfg) == 0, "load areas");
    expect(cfg.n == 2, "two configured areas");
    expect(strcmp(cfg.area[0].path, "lrs-selftest/echos/ru.qico") == 0, "short area path");
    expect(strcmp(cfg.area[1].path, "lrs-selftest/explicit") == 0, "explicit area path");
    config_free(&cfg);
    expect(config_load(conf, &cfg) == 0, "reload areas");
    expect(config_adopt_area(&cfg, "extra") == 0, "adopt extra");
    expect(cfg.n == 3, "adopt grew the list");
    expect(strcmp(cfg.area[2].path, "lrs-selftest/echos/extra") == 0, "adopted path");
    expect(config_adopt_area(&cfg, "extra") == 2, "adopt twice is a no-op");
    expect(config_adopt_area(&cfg, "missing.tag") == 1, "missing base is not added");
    config_free(&cfg);
    text = read_all(conf, NULL);
    expect(text && strstr(text, "echodir lrs-selftest/echos\n\n[areas]\nechoarea ru.qico\n") != NULL,
        "areas stands in front of the echo list");
    expect(text && strstr(text, "echoarea extra\n") != NULL, "config gained echoarea extra");
    free(text);
    /* A hand edit may leave blank lines in the section. The next add
     * rewrites that block without them. */
    text = read_all(conf, NULL);
    if (text) {
        char *gap = strstr(text, "[areas]\n");
        FILE *edit;
        expect(gap != NULL, "areas keyword present");
        if (gap) {
            edit = fopen(conf, "wb");
            if (edit) {
                fwrite(text, 1, (size_t)(gap - text), edit);
                fputs("[areas]\n\n\n", edit);
                fputs(gap + strlen("[areas]\n"), edit);
                fclose(edit);
            }
        }
        free(text);
    }
    expect(config_load(conf, &cfg) == 0, "reload after a gapped edit");
    expect(config_adopt_area(&cfg, "more") == 0, "adopt more");
    config_free(&cfg);
    text = read_all(conf, NULL);
    expect(text && strstr(text, "[areas]\nechoarea ru.qico\nechoarea OTHER lrs-selftest/explicit\nechoarea extra\nechoarea more\n") != NULL,
        "section has no blank lines between areas");
    expect(text && strstr(text, "[areas]\n\n") == NULL, "no blank line after areas");
    free(text);
    /* Deleting the keyword must not split the list. The next add puts
     * "areas" back in front of the echoarea lines at the end. */
    text = read_all(conf, NULL);
    if (text) {
        char *kw = strstr(text, "[areas]\n");
        FILE *edit;
        expect(kw != NULL, "areas keyword before the recovery check");
        if (kw) {
            memmove(kw, kw + strlen("[areas]\n"), strlen(kw + strlen("[areas]\n")) + 1);
            edit = fopen(conf, "wb");
            if (edit) {
                fputs(text, edit);
                fclose(edit);
            }
        }
        free(text);
    }
    fp = fopen("lrs-selftest/echos/third.sqd", "wb");
    if (fp)
        fclose(fp);
    expect(config_load(conf, &cfg) == 0, "reload after areas was deleted");
    expect(config_adopt_area(&cfg, "third") == 0, "adopt third restores areas");
    config_free(&cfg);
    text = read_all(conf, NULL);
    expect(text && strstr(text, "[areas]\nechoarea ru.qico\nechoarea OTHER lrs-selftest/explicit\nechoarea extra\nechoarea more\nechoarea third\n") != NULL,
        "deleted areas keyword was restored");
    expect(text && strstr(text, "echodir lrs-selftest/echos\n\n[areas]\n") != NULL,
        "settings above areas were left in place");
    free(text);
    /* An echoarea whose base was deleted is taken out of the config.
     * The neighbouring lines stay, with no blank left behind. */
    fp = fopen(conf, "ab");
    if (fp) {
        fputs("echoarea ghost lrs-selftest/missing\n", fp);
        fclose(fp);
    }
    expect(config_load(conf, &cfg) == 0, "reload with a deleted base");
    expect(config_forget_area(&cfg, "ghost") == 0, "forget ghost");
    expect(config_has_area(&cfg, "ghost") == 0, "ghost left memory");
    config_free(&cfg);
    text = read_all(conf, NULL);
    expect(text && strstr(text, "echoarea ghost") == NULL, "ghost left the file");
    expect(text && strstr(text, "echoarea third\n") != NULL, "last real area stayed");
    free(text);
    {
        Sq sq;
        expect(sq_open(&sq, "lrs-selftest/missing", 0, 0) == 2, "missing base is not an error");
    }
}

int selftest(void)
{
    g_fail = 0;
    test_decide();
    test_bso();
    test_lrp_text();
    test_state();
    test_squish();
    test_jam();
    test_log();
    test_areas();
    if (g_fail) {
        fprintf(stderr, "selftest: %d failure%s\n", g_fail, g_fail == 1 ? "" : "s");
        return 1;
    }
    printf("selftest: ok\n");
    return 0;
}
