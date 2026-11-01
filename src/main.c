#include "config.h"
#include "sync.h"
#include "version.h"

#include <stdio.h>
#include <string.h>

int selftest(void);

/*
 * Command summary shared by a bad invocation (stderr, exit 2) and by
 * --help (stdout, after the program banner). --help itself is not an
 * error, so the banner is not printed on the error path.
 */
static void commands(FILE *fp)
{
    fprintf(fp,
        "usage: lrs sync|show [-c lrs.conf]\n"
        "       lrs --help\n"
        "       lrs --version\n"
        "\n"
        "sync    apply inbound snapshots, add new point areas,\n"
        "        and queue one outbound file (cursors and position requests).\n"
        "        run after the tosser after a session, or after the editor exits\n"
        "show    print the current cursor of each configured area\n"
        "\n"
        "-c FILE            configuration file (default: lrs.conf)\n"
        "-h, --help         show this help and exit\n"
        "-v, -V, --version  show version and exit\n");
}

static void usage(void)
{
    commands(stderr);
}

/*
 * Program banner, then the command summary. The version here is the
 * plain package version. A -dev commit id is LRS_VERSION_ID and is
 * printed by --version, so this text stays the same across commits.
 * qico does the same split: the qcc screen shows PACKAGE_VERSION,
 * and qico -v shows version_id.
 */
static void help(void)
{
    printf(
        "%s %s\n"
        "Copyright (C) 2026 Alex M (2:203/910)\n"
        "License: GNU General Public License v3 or later\n"
        "https://github.com/glasslike/lrs\n"
        "\n"
        "Lastread sync. Keep lastread cursors synchronized between client and server.\n"
        "\n",
        LRS_NAME, LRS_VERSION);
    commands(stdout);
}

/* "name v<version id>". */
static void show_version(void)
{
    printf("%s v%s\n", LRS_NAME, LRS_VERSION_ID);
}

/*
 * -h/--help and -v/-V/--version may sit anywhere on the command line.
 * The word after -c is a path, so it is not read as an option. The
 * first such flag wins. 1 = help, 2 = version, 0 = neither.
 */
static int info_flag(int argc, char **argv)
{
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
            return 1;
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "-V") == 0 ||
            strcmp(argv[i], "--version") == 0)
            return 2;
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc)
            i++;
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *cmd;
    const char *cfgpath = "lrs.conf";
    int i;
    int info;
    Config cfg;
    int rc;

    info = info_flag(argc, argv);
    if (info == 1) {
        help();
        return 0;
    }
    if (info == 2) {
        show_version();
        return 0;
    }
    if (argc < 2) {
        usage();
        return 2;
    }
    cmd = argv[1];
    if (strcmp(cmd, "selftest") == 0)
        return selftest();
    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            cfgpath = argv[++i];
            continue;
        }
        usage();
        return 2;
    }
    if (config_load(cfgpath, &cfg) != 0)
        return 1;
    /* A log file exists only when the config names one. An empty path
     * leaves stderr as the only place messages are printed. */
    if (cfg.log[0])
        log_open(cfg.log);
    log_msg("%s", cmd);
    if (strcmp(cmd, "sync") == 0)
        rc = cmd_sync(&cfg);
    else if (strcmp(cmd, "show") == 0)
        rc = cmd_show(&cfg);
    else {
        usage();
        rc = 2;
    }
    if (rc != 0)
        log_msg("%s finished with status %d", cmd, rc);
    log_close();
    config_free(&cfg);
    return rc;
}
