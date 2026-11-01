/*
 * Stress the cursor protocol on the copied rabarber.local bases.
 * Originals under Testing/msgbasedir_* stay untouched. Each case
 * works on Testing/run copies. The mailer is the move of *.lrs from
 * one outbound into the other inbound.
 */
#include "config.h"
#include "squish.h"
#include "sync.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#endif

static int g_fail;
static int g_ran;

static const uint32_t k_node[] = {0, 21, 40, 75, 80, 82, 84, 85};
static const uint32_t k_point[] = {0, 1, 2, 3, 4, 5, 6, 7};

static void expect(int cond, const char *what)
{
    g_ran++;
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", what);
        g_fail++;
    }
}

static int copy_file(const char *src, const char *dst)
{
#ifdef _WIN32
    return CopyFileA(src, dst, FALSE) ? 0 : -1;
#else
    FILE *in = fopen(src, "rb");
    FILE *out;
    char buf[8192];
    size_t n;
    if (!in)
        return -1;
    out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        return -1;
    }
    while ((n = fread(buf, 1, sizeof buf, in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            fclose(in);
            fclose(out);
            return -1;
        }
    }
    fclose(in);
    return fclose(out) == 0 ? 0 : -1;
#endif
}

static void set_mtime(const char *path, time_t when)
{
#ifdef _WIN32
    HANDLE h;
    FILETIME ft;
    ULARGE_INTEGER u;
    h = CreateFileA(path, FILE_WRITE_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return;
    u.QuadPart = ((unsigned long long)when + 11644473600ULL) * 10000000ULL;
    ft.dwLowDateTime = (DWORD)u.LowPart;
    ft.dwHighDateTime = (DWORD)u.HighPart;
    SetFileTime(h, NULL, NULL, &ft);
    CloseHandle(h);
#else
    struct timespec ts[2];
    ts[0].tv_sec = when;
    ts[1].tv_sec = when;
    ts[0].tv_nsec = 0;
    ts[1].tv_nsec = 0;
    utimensat(AT_FDCWD, path, ts, 0);
#endif
}

static int set_pos(const char *base, uint32_t msgno, time_t when)
{
    Sq sq;
    char sql[LRP_PATH];
    if (sq_open(&sq, base, 0, 0) != 0)
        return -1;
    if (sq_write_lastread(&sq, msgno) != 0) {
        sq_close(&sq);
        return -1;
    }
    sq_close(&sq);
    snprintf(sql, sizeof sql, "%s.sql", base);
    set_mtime(sql, when);
    return 0;
}

static int read_pos_msgid(const char *base, char *msgid, size_t cap, uint32_t *msgno)
{
    Sq sq;
    uint32_t n = 0;
    int rc;
    if (sq_open(&sq, base, 0, 0) != 0)
        return -1;
    if (sq_read_lastread(&sq, &n, NULL) != 0) {
        sq_close(&sq);
        return -1;
    }
    rc = sq_msgid_of(&sq, n, msgid, cap, NULL);
    sq_close(&sq);
    if (msgno)
        *msgno = n;
    return rc == 0 ? 0 : -1;
}

static void write_cfg(const char *path, int server, const char *root)
{
    FILE *fp = fopen(path, "wb");
    if (!fp)
        return;
    fprintf(fp,
        "address %s\n"
        "peer %s\n"
        "squishuser 0\n"
        "inbound %s/in\n"
        "outbound %s/out\n"
        "state %s/lrs.state\n"
        "echodir %s/msg\n"
        "%s"
        "[areas]\n"
        "echoarea rabarber.local\n",
        server ? "2:203/910" : "2:203/910.1",
        server ? "2:203/910.1" : "2:203/910",
        root, root, root, root,
        server ? "server yes\n" : "");
    fclose(fp);
}

static int sync_side(const char *conf)
{
    Config cfg;
    int rc;
    if (config_load(conf, &cfg) != 0)
        return -1;
    rc = cmd_sync(&cfg);
    config_free(&cfg);
    return rc;
}

/* Move every snapshot the sender queued into the receiver inbound.
 * That is the mailer. The flow file is left behind; lrs reads *.lrs. */
static int deliver(const char *from_out, const char *to_in)
{
    int n = 0;
#ifdef _WIN32
    char pat[LRP_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    snprintf(pat, sizeof pat, "%s\\*.lrs", from_out);
    h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    do {
        char src[LRP_PATH];
        char dst[LRP_PATH];
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        snprintf(src, sizeof src, "%s\\%s", from_out, fd.cFileName);
        snprintf(dst, sizeof dst, "%s\\%s", to_in, fd.cFileName);
        if (!MoveFileExA(src, dst, MOVEFILE_REPLACE_EXISTING))
            return -1;
        n++;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    (void)from_out;
    (void)to_in;
#endif
    return n;
}

static int count_lrs(const char *dir)
{
    int n = 0;
#ifdef _WIN32
    char pat[LRP_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    snprintf(pat, sizeof pat, "%s\\*.lrs", dir);
    h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            n++;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#endif
    return n;
}

static void wipe_tree(const char *root)
{
#ifdef _WIN32
    char cmd[LRP_PATH * 2];
    snprintf(cmd, sizeof cmd, "rmdir /s /q \"%s\"", root);
    system(cmd);
#else
    (void)root;
#endif
}

static int prep(void)
{
    const char *dirs[] = {
        "Testing/run",
        "Testing/run/node", "Testing/run/node/in", "Testing/run/node/out",
        "Testing/run/node/msg",
        "Testing/run/point", "Testing/run/point/in", "Testing/run/point/out",
        "Testing/run/point/msg"
    };
    size_t i;
    wipe_tree("Testing/run");
    for (i = 0; i < sizeof dirs / sizeof dirs[0]; i++) {
#ifdef _WIN32
        if (!CreateDirectoryA(dirs[i], NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
            return -1;
#endif
    }
    if (copy_file("Testing/msgbasedir_910/Rabarber.local.sqd",
            "Testing/run/node/msg/Rabarber.local.sqd") != 0
        || copy_file("Testing/msgbasedir_910/Rabarber.local.sqi",
            "Testing/run/node/msg/Rabarber.local.sqi") != 0
        || copy_file("Testing/msgbasedir_910/Rabarber.local.sql",
            "Testing/run/node/msg/Rabarber.local.sql") != 0
        || copy_file("Testing/msgbasedir_910.1/rabarber.local.sqd",
            "Testing/run/point/msg/rabarber.local.sqd") != 0
        || copy_file("Testing/msgbasedir_910.1/rabarber.local.sqi",
            "Testing/run/point/msg/rabarber.local.sqi") != 0
        || copy_file("Testing/msgbasedir_910.1/rabarber.local.sql",
            "Testing/run/point/msg/rabarber.local.sql") != 0)
        return -1;
    write_cfg("Testing/run/node/lrs.conf", 1, "Testing/run/node");
    write_cfg("Testing/run/point/lrs.conf", 0, "Testing/run/point");
    return 0;
}

static const char *node_base = "Testing/run/node/msg/Rabarber.local";
static const char *point_base = "Testing/run/point/msg/rabarber.local";

/* Exchange whatever is queued, then let each side import and publish
 * until neither outbound has a snapshot. */
static int pump(int rounds)
{
    int i;
    for (i = 0; i < rounds; i++) {
        int dn = deliver("Testing/run/node/out", "Testing/run/point/in");
        int dp = deliver("Testing/run/point/out", "Testing/run/node/in");
        int sn, sp;
        if (dn < 0 || dp < 0)
            return -1;
        sn = sync_side("Testing/run/node/lrs.conf");
        sp = sync_side("Testing/run/point/lrs.conf");
        if (sn != 0 || sp != 0)
            return -1;
        if (count_lrs("Testing/run/node/out") == 0
            && count_lrs("Testing/run/point/out") == 0
            && count_lrs("Testing/run/node/in") == 0
            && count_lrs("Testing/run/point/in") == 0)
            return 0;
    }
    return -1;
}

static int msgid_at(int point, int pos, char *out, size_t cap)
{
    const char *base = point ? point_base : node_base;
    uint32_t msg = point ? k_point[pos] : k_node[pos];
    Sq sq;
    int rc;
    if (sq_open(&sq, base, 0, 0) != 0)
        return -1;
    rc = sq_msgid_of(&sq, msg, out, cap, NULL);
    sq_close(&sq);
    return rc == 0 ? 0 : -1;
}

static int same_msgid(char *got_n, char *got_p)
{
    uint32_t mn = 0;
    uint32_t mp = 0;
    if (read_pos_msgid(node_base, got_n, LRP_MSGID, &mn) != 0)
        return -1;
    if (read_pos_msgid(point_base, got_p, LRP_MSGID, &mp) != 0)
        return -1;
    return strcmp(got_n, got_p) == 0 ? 0 : 1;
}

/* Bring both memories up from empty state without moving lastread.
 * Both already sit on the same MSGID in the copied bases. */
static int handshake(void)
{
    if (sync_side("Testing/run/point/lrs.conf") != 0)
        return -1;
    if (deliver("Testing/run/point/out", "Testing/run/node/in") < 0)
        return -1;
    if (sync_side("Testing/run/node/lrs.conf") != 0)
        return -1;
    if (deliver("Testing/run/node/out", "Testing/run/point/in") < 0)
        return -1;
    if (sync_side("Testing/run/point/lrs.conf") != 0)
        return -1;
    if (deliver("Testing/run/point/out", "Testing/run/node/in") < 0)
        return -1;
    if (sync_side("Testing/run/node/lrs.conf") != 0)
        return -1;
    return pump(4);
}

static void check_pair(const char *title, int want_pos)
{
    char nn[LRP_MSGID];
    char pp[LRP_MSGID];
    char want[LRP_MSGID];
    int sm;
    sm = same_msgid(nn, pp);
    expect(sm == 0, title);
    if (sm != 0)
        fprintf(stderr, "  node [%s]\n  point [%s]\n", nn, pp);
    if (want_pos >= 0 && msgid_at(0, want_pos, want, sizeof want) == 0) {
        char line[256];
        snprintf(line, sizeof line, "%s winner", title);
        expect(sm == 0 && strcmp(nn, want) == 0, line);
        if (sm == 0 && strcmp(nn, want) != 0)
            fprintf(stderr, "  have [%s]\n  want [%s]\n", nn, want);
    }
}

/* One side moves, the other does not. The mover must win. */
static void case_one_moves(int mover_point, int pos, time_t when)
{
    char title[80];
    expect(prep() == 0, "prep one-move");
    expect(handshake() == 0, "handshake one-move");
    if (mover_point)
        expect(set_pos(point_base, k_point[pos], when) == 0, "set point");
    else
        expect(set_pos(node_base, k_node[pos], when) == 0, "set node");
    /* The side that moved publishes first. */
    if (mover_point)
        expect(sync_side("Testing/run/point/lrs.conf") == 0, "point export");
    else
        expect(sync_side("Testing/run/node/lrs.conf") == 0, "node export");
    expect(pump(6) == 0, "pump one-move");
    snprintf(title, sizeof title, "only %s moves to %d", mover_point ? "point" : "node", pos);
    check_pair(title, pos);
}

/* Position 3 is where both copies already sit. Writing it again
 * changes the file time and not the cursor, so it is not a move. */
static int winner_pos(int pos_n, int pos_p, time_t tn, time_t tp)
{
    int node_moved = pos_n != 3;
    int point_moved = pos_p != 3;
    if (node_moved && !point_moved)
        return pos_n;
    if (point_moved && !node_moved)
        return pos_p;
    if (tn > tp)
        return pos_n;
    if (tp > tn)
        return pos_p;
    /* Equal clocks: the point address is greater. */
    return pos_p;
}

/* Both lastreads change, then both publish before either imports. */
static void case_both(int pos_n, int pos_p, time_t tn, time_t tp, const char *title)
{
    expect(prep() == 0, "prep both");
    expect(handshake() == 0, "handshake both");
    expect(set_pos(node_base, k_node[pos_n], tn) == 0, "set node both");
    expect(set_pos(point_base, k_point[pos_p], tp) == 0, "set point both");
    expect(sync_side("Testing/run/node/lrs.conf") == 0, "node publishes");
    expect(sync_side("Testing/run/point/lrs.conf") == 0, "point publishes");
    expect(pump(8) == 0, "pump both");
    check_pair(title, winner_pos(pos_n, pos_p, tn, tp));
}

int main(void)
{
    int pos;
    int a, b;
    time_t tn, tp;
    int i;
    g_fail = 0;
    g_ran = 0;
    fprintf(stderr, "stress: handshake and single moves\n");
    expect(prep() == 0, "prep idle");
    expect(handshake() == 0, "handshake idle");
    check_pair("idle stays on the shared msgid", 3);

    for (pos = 0; pos <= 7; pos++) {
        case_one_moves(0, pos, 1700000000);
        case_one_moves(1, pos, 1700000000);
    }

    fprintf(stderr, "stress: both move, clocks differ\n");
    for (a = 0; a <= 7; a++) {
        for (b = 0; b <= 7; b++) {
            char title[96];
            if (a == 3 && b == 3)
                continue;
            snprintf(title, sizeof title, "node %d newer than point %d", a, b);
            case_both(a, b, 1700000200, 1700000100, title);
            snprintf(title, sizeof title, "point %d newer than node %d", b, a);
            case_both(a, b, 1700000100, 1700000200, title);
        }
    }

    fprintf(stderr, "stress: both move, equal clocks, point address wins\n");
    for (a = 0; a <= 7; a++) {
        for (b = 0; b <= 7; b++) {
            char title[96];
            if (a == b)
                continue;
            snprintf(title, sizeof title, "tie node %d point %d", a, b);
            case_both(a, b, 1700000500, 1700000500, title);
        }
    }

    fprintf(stderr, "stress: random order of publish\n");
    for (i = 0; i < 40; i++) {
        int order = i % 3;
        char title[96];
        a = i % 8;
        b = (i * 3) % 8;
        tn = 1700001000 + (i % 5) * 10;
        tp = 1700001000 + ((i / 5) % 5) * 10;
        expect(prep() == 0, "prep random");
        expect(handshake() == 0, "handshake random");
        expect(set_pos(node_base, k_node[a], tn) == 0, "rand node");
        expect(set_pos(point_base, k_point[b], tp) == 0, "rand point");
        if (order == 0) {
            expect(sync_side("Testing/run/node/lrs.conf") == 0, "rand n");
            expect(deliver("Testing/run/node/out", "Testing/run/point/in") >= 0, "rand d1");
            expect(sync_side("Testing/run/point/lrs.conf") == 0, "rand p");
        } else if (order == 1) {
            expect(sync_side("Testing/run/point/lrs.conf") == 0, "rand p2");
            expect(deliver("Testing/run/point/out", "Testing/run/node/in") >= 0, "rand d2");
            expect(sync_side("Testing/run/node/lrs.conf") == 0, "rand n2");
        } else {
            expect(sync_side("Testing/run/node/lrs.conf") == 0, "rand n3");
            expect(sync_side("Testing/run/point/lrs.conf") == 0, "rand p3");
        }
        expect(pump(8) == 0, "pump random");
        snprintf(title, sizeof title, "random %d order %d", i, order);
        /* Import sees the on-disk move before it applies the peer,
         * so the delivery order does not change the winner. */
        check_pair(title, winner_pos(a, b, tn, tp));
    }

    fprintf(stderr, "stress: %d checks, %d failed\n", g_ran, g_fail);
    return g_fail ? 1 : 0;
}
