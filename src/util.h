#ifndef LRP_UTIL_H
#define LRP_UTIL_H

/*
 * Small helpers shared by the rest of the program.
 *
 * Squish stores every integer little-endian. The readers below never
 * cast a struct over the file, so the same code is correct on a
 * big-endian machine and does not depend on compiler packing.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define LRP_PATH 1024
#define LRP_TAG 80
#define LRP_MSGID 201
#define LRP_ADDR 64
#define LRP_LINE 2048

void log_msg(const char *fmt, ...);

/*
 * Append log_msg lines to a file as well as stderr. The file line is
 * stamped with local time so a cron run can be read later; stderr keeps
 * the short "lrs: ..." form used at the console. An empty path, or a
 * path that cannot be opened, leaves stderr as the only sink. Opening
 * the log is not fatal: a missing log must not stop a sync.
 */
void log_open(const char *path);
void log_close(void);

int str_icmp(const char *a, const char *b);
void str_trim(char *s);
char *str_dup(const char *s);

int path_join(char *dst, size_t cap, const char *dir, const char *name);
const char *path_base(const char *path);
int path_exists(const char *path);
int path_mtime(const char *path, int64_t *mtime);

uint16_t rd_u16(const unsigned char *p);
uint32_t rd_u32(const unsigned char *p);
void wr_u32(unsigned char *p, uint32_t v);

int write_atomic(const char *path, const char *text);
/* Push stdio buffers to the OS. 0, or -1 if the flush or the sync failed. */
int file_sync(FILE *fp);
char *read_all(const char *path, size_t *len);

int addr_parse(const char *s, unsigned *zone, unsigned *net, unsigned *node, unsigned *point);
int addr_cmp_str(const char *a, const char *b);

/*
 * Build the normal-flavor BSO flow and busy paths for one peer.
 *
 * Both sides of an lrs link are in the same zone and share one outbound
 * directory, the path from the config. qico can also append ".ZZZ" when
 * the destination zone differs from the primary AKA; that branch is not
 * used here. The directory is taken as given.
 *
 * Inside it the names match qico's get_outbound_name() (BSO),
 * get_flo_name() with flavor F_NORM, and get_busy_name() with letter 'b'.
 * Net and node are four lowercase hex digits each:
 *
 *   {outbound}/{net:04x}{node:04x}.flo
 *   {outbound}/{net:04x}{node:04x}.bsy
 *
 * A non-zero point inserts the .pnt directory qico creates for points:
 *
 *   {outbound}/{net:04x}{node:04x}.pnt/{point:08x}.flo
 *
 * The zone field of the peer address is not part of the file name.
 * Hold, crash, direct and immediate flavors (.hlo/.clo/.dlo/.ilo) are
 * not used: lastread snapshots travel as ordinary mail.
 *
 * Returns 0, or -1 if the peer address will not parse or a result does
 * not fit.
 */
int bso_flo_bsy(const char *outbound, const char *peer,
    char *flo, size_t flo_cap, char *bsy, size_t bsy_cap);

/*
 * Create every directory that must exist before "filepath" can be
 * created. A point flow file lives in NNNNFFFF.pnt, which is not there
 * until the first file for that point is queued. Existing directories,
 * and a Windows drive prefix such as "D:", are left alone. Returns 0
 * when the parent directory is present afterwards.
 */
int path_ensure_parent(const char *filepath);

int64_t ymd_hms_unix(int year, int month, int day, int hour, int min, int sec);
int unix_ymdhms(int64_t t, int *year, int *month, int *day, int *hour, int *min, int *sec);
int format_unix(int64_t t, char *dst, size_t cap);
int parse_unix(const char *s, int64_t *out);

typedef struct StrList {
    char **item;
    size_t n;
    size_t cap;
} StrList;

void strlist_free(StrList *list);
int strlist_add(StrList *list, const char *s);
int dir_list(const char *dir, StrList *names);

#endif
