#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void config_free(Config *cfg)
{
    if (!cfg)
        return;
    free(cfg->area);
    cfg->area = NULL;
    cfg->n = 0;
}

static int add_area(Config *cfg, const char *tag, const char *path)
{
    CfgArea *grown;
    if (!tag || !tag[0] || strlen(tag) >= LRP_TAG)
        return -1;
    if (path && strlen(path) >= LRP_PATH)
        return -1;
    grown = (CfgArea *)realloc(cfg->area, (cfg->n + 1) * sizeof(CfgArea));
    if (!grown)
        return -1;
    cfg->area = grown;
    memset(&cfg->area[cfg->n], 0, sizeof(CfgArea));
    snprintf(cfg->area[cfg->n].tag, sizeof cfg->area[cfg->n].tag, "%s", tag);
    if (path)
        snprintf(cfg->area[cfg->n].path, sizeof cfg->area[cfg->n].path, "%s", path);
    cfg->n++;
    return 0;
}

/* An echotag used as a filename. A slash would escape echodir. */
static int tag_ok(const char *tag)
{
    if (!tag || !tag[0] || strlen(tag) >= LRP_TAG)
        return 0;
    if (strcmp(tag, ".") == 0 || strcmp(tag, "..") == 0)
        return 0;
    if (strchr(tag, '/') || strchr(tag, '\\') || strchr(tag, ':'))
        return 0;
    return 1;
}

/* .sqd is the Squish data file, .jhr the JAM header file. Either one
 * names an echo. The stem is the tag, without the extension. */
static int is_base_name(const char *name, char *stem, size_t cap, int *jam)
{
    size_t n;
    int which;
    if (!name)
        return 0;
    n = strlen(name);
    if (n >= 5 && str_icmp(name + n - 4, ".sqd") == 0)
        which = 0;
    else if (n >= 5 && str_icmp(name + n - 4, ".jhr") == 0)
        which = 1;
    else
        return 0;
    n -= 4;
    if (n >= cap)
        return 0;
    memcpy(stem, name, n);
    stem[n] = '\0';
    if (!tag_ok(stem))
        return 0;
    if (jam)
        *jam = which;
    return 1;
}

int config_has_area(const Config *cfg, const char *tag)
{
    size_t i;
    if (!cfg || !tag)
        return 0;
    for (i = 0; i < cfg->n; i++) {
        if (str_icmp(cfg->area[i].tag, tag) == 0)
            return 1;
    }
    return 0;
}

/* Locate echodir/<tag> as a Squish or a JAM base. The stored path has
 * no extension. On a case-sensitive disk the tag in a snapshot may not
 * match the filename's case, so a miss falls back to a scan and returns
 * the name that is actually on disk. Both formats for one tag is not a
 * choice: return 2 and leave the echo unconfigured. */
static int find_base(const char *dir, const char *tag, char *base, size_t cap)
{
    char path[LRP_PATH];
    StrList names;
    size_t i;
    int saw_sq = 0;
    int saw_jam = 0;
    char stem_sq[LRP_TAG];
    char stem_jam[LRP_TAG];
    if (!dir || !dir[0] || !tag_ok(tag))
        return -1;
    if (path_join(base, cap, dir, tag) != 0)
        return -1;
    if (snprintf(path, sizeof path, "%s.sqd", base) >= (int)sizeof path)
        return -1;
    saw_sq = path_exists(path);
    if (snprintf(path, sizeof path, "%s.jhr", base) >= (int)sizeof path)
        return -1;
    saw_jam = path_exists(path);
    if (saw_sq && saw_jam)
        return 2;
    if (saw_sq || saw_jam)
        return 0;
    memset(&names, 0, sizeof names);
    stem_sq[0] = '\0';
    stem_jam[0] = '\0';
    if (dir_list(dir, &names) != 0) {
        strlist_free(&names);
        return -1;
    }
    for (i = 0; i < names.n; i++) {
        char stem[LRP_TAG];
        int jam = 0;
        if (!is_base_name(names.item[i], stem, sizeof stem, &jam))
            continue;
        if (str_icmp(stem, tag) != 0)
            continue;
        if (jam) {
            saw_jam = 1;
            snprintf(stem_jam, sizeof stem_jam, "%s", stem);
        } else {
            saw_sq = 1;
            snprintf(stem_sq, sizeof stem_sq, "%s", stem);
        }
    }
    strlist_free(&names);
    if (saw_sq && saw_jam)
        return 2;
    if (!saw_sq && !saw_jam)
        return 1;
    if (path_join(base, cap, dir, saw_jam ? stem_jam : stem_sq) != 0)
        return -1;
    return 0;
}

/* Section header, written like the bracketed sections used in other
 * configs. Older forms are still recognized and rewritten to this. */
#define AREA_MARK "[areas]"

static int line_blank(const char *s)
{
    while (*s == ' ' || *s == '\t' || *s == '\r')
        s++;
    return *s == '\0';
}

static int line_is_mark(const char *s)
{
    while (*s == ' ' || *s == '\t')
        s++;
    if (strcmp(s, AREA_MARK) == 0 || strcmp(s, "areas") == 0)
        return 1;
    return strcmp(s, "# lrs-areas") == 0;
}

static int line_is_echoarea(const char *s)
{
    while (*s == ' ' || *s == '\t')
        s++;
    return strncmp(s, "echoarea", 8) == 0
        && (s[8] == '\0' || s[8] == ' ' || s[8] == '\t');
}

/* Put `line` at the end of the [areas] block. Text above the header is
 * copied as the user left it, except trailing blank lines, which become
 * exactly one blank line before the header. Inside the block, blank
 * lines are removed. If the header was deleted, the echoarea lines
 * already sitting at the end of the file are taken as that block and
 * [areas] is written back in front of them. Older headers ("areas" and
 * "# lrs-areas") are accepted once and replaced. The newline style of
 * the file is kept. */
static int append_area_line(const char *file, const char *line)
{
    char *text;
    size_t len = 0;
    const char *nl = "\n";
    char **rows = NULL;
    size_t n = 0;
    size_t cap = 0;
    size_t i;
    size_t mark = (size_t)-1;
    size_t section;
    size_t pre_n;
    size_t out_cap;
    size_t used = 0;
    char *out;
    const char *p;
    int rc;
    if (!file || !file[0] || !line || !line[0])
        return -1;
    text = read_all(file, &len);
    if (!text)
        return -1;
    if (len >= 2) {
        size_t k;
        for (k = 0; k + 1 < len; k++) {
            if (text[k] == '\r' && text[k + 1] == '\n') {
                nl = "\r\n";
                break;
            }
        }
    }
    p = text;
    while (p < text + len) {
        const char *eol = p;
        size_t row_len;
        char *row;
        char **grown;
        while (eol < text + len && *eol != '\n')
            eol++;
        row_len = (size_t)(eol - p);
        if (row_len > 0 && p[row_len - 1] == '\r')
            row_len--;
        if (n == cap) {
            cap = cap ? cap * 2 : 16;
            grown = (char **)realloc(rows, cap * sizeof(char *));
            if (!grown) {
                free(text);
                goto fail_rows;
            }
            rows = grown;
        }
        row = (char *)malloc(row_len + 1);
        if (!row) {
            free(text);
            goto fail_rows;
        }
        memcpy(row, p, row_len);
        row[row_len] = '\0';
        rows[n++] = row;
        if (eol >= text + len)
            break;
        p = eol + 1;
    }
    free(text);
    text = NULL;
    for (i = 0; i < n; i++) {
        if (line_is_mark(rows[i]))
            mark = i;
    }
    if (mark != (size_t)-1) {
        pre_n = mark;
        section = mark + 1;
    } else {
        /* No header. A run of echoarea lines at the end of the file
         * is still the section: the keyword is put back in front. */
        size_t end = n;
        while (end > 0 && line_blank(rows[end - 1]))
            end--;
        section = end;
        while (section > 0
            && (line_blank(rows[section - 1]) || line_is_echoarea(rows[section - 1])))
            section--;
        if (section == end)
            section = n;
        pre_n = section == n ? n : section;
    }
    while (pre_n > 0 && line_blank(rows[pre_n - 1]))
        pre_n--;
    out_cap = len + strlen(line) + 64;
    for (i = 0; i < n; i++)
        out_cap += strlen(rows[i]) + 2;
    out = (char *)malloc(out_cap);
    if (!out)
        goto fail_rows;
    out[0] = '\0';
    for (i = 0; i < pre_n; i++) {
        int nw = snprintf(out + used, out_cap - used, "%s%s", rows[i], nl);
        if (nw < 0 || (size_t)nw >= out_cap - used) {
            free(out);
            goto fail_rows;
        }
        used += (size_t)nw;
    }
    if (pre_n > 0) {
        int nw = snprintf(out + used, out_cap - used, "%s", nl);
        if (nw < 0 || (size_t)nw >= out_cap - used) {
            free(out);
            goto fail_rows;
        }
        used += (size_t)nw;
    }
    {
        int nw = snprintf(out + used, out_cap - used, "%s%s", AREA_MARK, nl);
        if (nw < 0 || (size_t)nw >= out_cap - used) {
            free(out);
            goto fail_rows;
        }
        used += (size_t)nw;
    }
    for (i = section; i < n; i++) {
        int nw;
        if (line_blank(rows[i]) || line_is_mark(rows[i]))
            continue;
        nw = snprintf(out + used, out_cap - used, "%s%s", rows[i], nl);
        if (nw < 0 || (size_t)nw >= out_cap - used) {
            free(out);
            goto fail_rows;
        }
        used += (size_t)nw;
    }
    {
        int nw = snprintf(out + used, out_cap - used, "%s%s", line, nl);
        if (nw < 0 || (size_t)nw >= out_cap - used) {
            free(out);
            goto fail_rows;
        }
    }
    rc = write_atomic(file, out);
    free(out);
    for (i = 0; i < n; i++)
        free(rows[i]);
    free(rows);
    return rc;
fail_rows:
    for (i = 0; i < n; i++)
        free(rows[i]);
    free(rows);
    return -1;
}

/* True when the line is "echoarea TAG ..." for this tag. */
static int echoarea_matches(const char *line, const char *tag)
{
    const char *s = line;
    char got[LRP_TAG];
    size_t n = 0;
    while (*s == ' ' || *s == '\t')
        s++;
    if (strncmp(s, "echoarea", 8) != 0 || (s[8] != ' ' && s[8] != '\t'))
        return 0;
    s += 8;
    while (*s == ' ' || *s == '\t')
        s++;
    while (*s && *s != ' ' && *s != '\t' && *s != '\r' && n + 1 < sizeof got)
        got[n++] = *s++;
    got[n] = '\0';
    return str_icmp(got, tag) == 0;
}

int config_forget_area(Config *cfg, const char *tag)
{
    char *text;
    size_t len = 0;
    const char *nl = "\n";
    char **rows = NULL;
    size_t n = 0;
    size_t cap = 0;
    size_t i;
    size_t w;
    size_t out_cap;
    size_t used = 0;
    char *out;
    const char *p;
    int rc;
    if (!cfg || !tag || !tag[0] || !cfg->file[0])
        return -1;
    text = read_all(cfg->file, &len);
    if (!text)
        return -1;
    if (len >= 2) {
        size_t k;
        for (k = 0; k + 1 < len; k++) {
            if (text[k] == '\r' && text[k + 1] == '\n') {
                nl = "\r\n";
                break;
            }
        }
    }
    p = text;
    while (p < text + len) {
        const char *eol = p;
        size_t row_len;
        char *row;
        char **grown;
        while (eol < text + len && *eol != '\n')
            eol++;
        row_len = (size_t)(eol - p);
        if (row_len > 0 && p[row_len - 1] == '\r')
            row_len--;
        if (n == cap) {
            cap = cap ? cap * 2 : 16;
            grown = (char **)realloc(rows, cap * sizeof(char *));
            if (!grown) {
                free(text);
                goto fail_rows;
            }
            rows = grown;
        }
        row = (char *)malloc(row_len + 1);
        if (!row) {
            free(text);
            goto fail_rows;
        }
        memcpy(row, p, row_len);
        row[row_len] = '\0';
        rows[n++] = row;
        if (eol >= text + len)
            break;
        p = eol + 1;
    }
    free(text);
    out_cap = len + 1;
    out = (char *)malloc(out_cap);
    if (!out)
        goto fail_rows;
    out[0] = '\0';
    for (i = 0; i < n; i++) {
        int nw;
        if (echoarea_matches(rows[i], tag))
            continue;
        nw = snprintf(out + used, out_cap - used, "%s%s", rows[i], nl);
        if (nw < 0 || (size_t)nw >= out_cap - used) {
            free(out);
            goto fail_rows;
        }
        used += (size_t)nw;
    }
    rc = write_atomic(cfg->file, out);
    free(out);
    for (i = 0; i < n; i++)
        free(rows[i]);
    free(rows);
    if (rc != 0)
        return -1;
    w = 0;
    for (i = 0; i < cfg->n; i++) {
        if (str_icmp(cfg->area[i].tag, tag) == 0)
            continue;
        if (w != i)
            cfg->area[w] = cfg->area[i];
        w++;
    }
    cfg->n = w;
    return 0;
fail_rows:
    for (i = 0; i < n; i++)
        free(rows[i]);
    free(rows);
    return -1;
}

int config_adopt_area(Config *cfg, const char *tag)
{
    char base[LRP_PATH];
    char line[LRP_LINE];
    const char *stem;
    int found;
    if (!cfg)
        return -1;
    if (config_has_area(cfg, tag))
        return 2;
    if (!cfg->echodir[0]) {
        log_msg("%s: not in config and echodir is not set", tag ? tag : "");
        return 1;
    }
    if (!tag_ok(tag)) {
        log_msg("skip area tag %s", tag ? tag : "");
        return 1;
    }
    found = find_base(cfg->echodir, tag, base, sizeof base);
    if (found == 2) {
        log_msg("%s: both Squish and JAM bases in %s, not added", tag, cfg->echodir);
        return 1;
    }
    if (found != 0) {
        if (found < 0)
            log_msg("cannot read echodir %s", cfg->echodir);
        else
            log_msg("%s: no base in %s", tag, cfg->echodir);
        return found < 0 ? -1 : 1;
    }
    /* Same spelling as the directory entry: the short form reloads.
     * A case difference needs the explicit path, or the next load on a
     * case-sensitive disk would look for the tag and miss the file. */
    stem = path_base(base);
    if (strcmp(stem, tag) == 0) {
        if (snprintf(line, sizeof line, "echoarea %s", tag) >= (int)sizeof line)
            return -1;
    } else if (snprintf(line, sizeof line, "echoarea %s %s", tag, base) >= (int)sizeof line) {
        return -1;
    }
    if (append_area_line(cfg->file, line) != 0) {
        log_msg("cannot append %s to %s", line, cfg->file);
        return -1;
    }
    if (add_area(cfg, tag, base) != 0)
        return -1;
    log_msg("added %s", line);
    return 0;
}

int config_discover(Config *cfg)
{
    StrList names;
    size_t i;
    if (!cfg || !cfg->echodir[0])
        return 0;
    memset(&names, 0, sizeof names);
    if (dir_list(cfg->echodir, &names) != 0) {
        log_msg("cannot read echodir %s", cfg->echodir);
        strlist_free(&names);
        return -1;
    }
    for (i = 0; i < names.n; i++) {
        char stem[LRP_TAG];
        int rc;
        if (!is_base_name(names.item[i], stem, sizeof stem, NULL))
            continue;
        if (config_has_area(cfg, stem))
            continue;
        /* adopt writes the config line and resolves the on-disk name. */
        rc = config_adopt_area(cfg, stem);
        if (rc < 0) {
            strlist_free(&names);
            return -1;
        }
    }
    strlist_free(&names);
    return 0;
}

/* echoarea tag [base]. The base is optional: without it the file is
 * echodir/tag, filled in once the whole config has been read so echodir
 * may appear below the area lines. */
static int parse_area(Config *cfg, const char *path, int lineno, char *val)
{
    char *pathv = strchr(val, ' ');
    if (pathv) {
        *pathv++ = '\0';
        str_trim(pathv);
        if (pathv[0] == '"') {
            size_t n = strlen(pathv);
            if (n >= 2 && pathv[n - 1] == '"') {
                pathv[n - 1] = '\0';
                pathv++;
            }
        }
        if (!pathv[0])
            pathv = NULL;
    }
    if (!tag_ok(val)) {
        log_msg("%s:%d: bad area tag %s", path, lineno, val);
        return -1;
    }
    if (config_has_area(cfg, val)) {
        log_msg("%s:%d: duplicate area %s", path, lineno, val);
        return 0;
    }
    return add_area(cfg, val, pathv);
}

static int resolve_areas(Config *cfg, const char *path)
{
    size_t i;
    for (i = 0; i < cfg->n; i++) {
        char base[LRP_PATH];
        if (cfg->area[i].path[0])
            continue;
        if (!cfg->echodir[0]) {
            log_msg("%s: echoarea %s needs echodir or an explicit base path",
                path, cfg->area[i].tag);
            return -1;
        }
        if (path_join(base, sizeof base, cfg->echodir, cfg->area[i].tag) != 0)
            return -1;
        snprintf(cfg->area[i].path, sizeof cfg->area[i].path, "%s", base);
    }
    return 0;
}

int config_load(const char *path, Config *cfg)
{
    FILE *fp;
    char line[LRP_LINE];
    int lineno = 0;
    memset(cfg, 0, sizeof *cfg);
    cfg->squishuser = 0;
    if (!path || strlen(path) >= sizeof cfg->file) {
        log_msg("config path is too long");
        return -1;
    }
    snprintf(cfg->file, sizeof cfg->file, "%s", path);
    fp = fopen(path, "rb");
    if (!fp) {
        log_msg("cannot open config %s", path);
        return -1;
    }
    while (fgets(line, sizeof line, fp)) {
        char *val;
        lineno++;
        /* fgets keeps the newline when the whole line fit. Without it,
         * and with more file left, the line was cut at LRP_LINE. */
        if (strchr(line, '\n') == NULL && !feof(fp)) {
            log_msg("%s:%d: line is too long", path, lineno);
            fclose(fp);
            config_free(cfg);
            return -1;
        }
        str_trim(line);
        if (line[0] == '\0' || line[0] == '#')
            continue;
        /* Section header. It has no value: the echoarea lines that
         * follow it are the block lrs rewrites. "areas" is the previous
         * spelling and still loads. */
        if (strcmp(line, "[areas]") == 0 || strcmp(line, "areas") == 0)
            continue;
        val = strchr(line, ' ');
        if (!val) {
            log_msg("%s:%d: expected a value", path, lineno);
            fclose(fp);
            config_free(cfg);
            return -1;
        }
        *val++ = '\0';
        str_trim(val);
        if (val[0] == '"') {
            size_t n = strlen(val);
            if (n >= 2 && val[n - 1] == '"') {
                val[n - 1] = '\0';
                val++;
            }
        }
        if (strcmp(line, "address") == 0)
            snprintf(cfg->address, sizeof cfg->address, "%s", val);
        else if (strcmp(line, "peer") == 0)
            snprintf(cfg->peer, sizeof cfg->peer, "%s", val);
        else if (strcmp(line, "squishuser") == 0)
            cfg->squishuser = atoi(val);
        else if (strcmp(line, "jamuser") == 0) {
            if (strlen(val) >= sizeof cfg->jamuser) {
                log_msg("%s:%d: jamuser is too long", path, lineno);
                fclose(fp);
                config_free(cfg);
                return -1;
            }
            snprintf(cfg->jamuser, sizeof cfg->jamuser, "%s", val);
        }
        else if (strcmp(line, "inbound") == 0)
            snprintf(cfg->inbound, sizeof cfg->inbound, "%s", val);
        else if (strcmp(line, "outbound") == 0)
            snprintf(cfg->outbound, sizeof cfg->outbound, "%s", val);
        else if (strcmp(line, "state") == 0)
            snprintf(cfg->state, sizeof cfg->state, "%s", val);
        /* Optional. Absent or empty: no log file is opened. */
        else if (strcmp(line, "log") == 0)
            snprintf(cfg->log, sizeof cfg->log, "%s", val);
        else if (strcmp(line, "server") == 0) {
            if (str_icmp(val, "yes") == 0 || strcmp(val, "1") == 0)
                cfg->server = 1;
            else if (str_icmp(val, "no") == 0 || strcmp(val, "0") == 0)
                cfg->server = 0;
            else {
                log_msg("%s:%d: server must be yes or no", path, lineno);
                fclose(fp);
                config_free(cfg);
                return -1;
            }
        }
        else if (strcmp(line, "echodir") == 0)
            snprintf(cfg->echodir, sizeof cfg->echodir, "%s", val);
        else if (strcmp(line, "[areas]") == 0 || strcmp(line, "areas") == 0) {
            log_msg("%s:%d: [areas] takes no value", path, lineno);
            fclose(fp);
            config_free(cfg);
            return -1;
        }
        else if (strcmp(line, "echoarea") == 0) {
            if (parse_area(cfg, path, lineno, val) != 0) {
                fclose(fp);
                config_free(cfg);
                return -1;
            }
        } else {
            log_msg("%s:%d: unknown keyword %s", path, lineno, line);
            fclose(fp);
            config_free(cfg);
            return -1;
        }
    }
    if (ferror(fp)) {
        log_msg("cannot read config %s", path);
        fclose(fp);
        config_free(cfg);
        return -1;
    }
    fclose(fp);
    if (!cfg->address[0] || !cfg->peer[0] || !cfg->inbound[0] || !cfg->outbound[0]
        || !cfg->state[0]) {
        log_msg("config needs address, peer, inbound, outbound and state");
        config_free(cfg);
        return -1;
    }
    if (cfg->n == 0 && !cfg->echodir[0]) {
        log_msg("config needs echoarea or echodir");
        config_free(cfg);
        return -1;
    }
    if (cfg->squishuser < 0) {
        log_msg("squishuser must be >= 0");
        config_free(cfg);
        return -1;
    }
    /* flo and bsy are not config keys. The mailer looks for the BSO name
     * of the peer inside the single outbound directory configured above.
     * Both partners are in one zone, so that directory is never rewritten
     * to outbound.ZZZ. */
    if (bso_flo_bsy(cfg->outbound, cfg->peer,
            cfg->flo, sizeof cfg->flo, cfg->bsy, sizeof cfg->bsy) != 0) {
        log_msg("cannot build flo/bsy names from address %s and peer %s",
            cfg->address, cfg->peer);
        config_free(cfg);
        return -1;
    }
    if (resolve_areas(cfg, path) != 0) {
        config_free(cfg);
        return -1;
    }
    return 0;
}
