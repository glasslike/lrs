#include "config.h"
#include "sync.h"
#include "version.h"

#include <stdio.h>
#include <string.h>

int selftest(void);

/*
 * The one-line form printed when the invocation is not a command.
 * Square brackets mark what may be left out: -c is optional because
 * lrs.conf in the current directory is the default. A required
 * argument would be written without brackets.
 */
static void usage_line(FILE *fp)
{
    fprintf(fp, "usage: lrs sync|show [-c lrs.conf]\n");
}

/*
 * Missing or unrecognized arguments. This is not --help: one sentence
 * about what is wrong, then the usage line, on stderr, exit 2.
 */
static void fail_usage(const char *why)
{
    fprintf(stderr, "%s\n", why);
    usage_line(stderr);
}

/*
 * Program banner, then the command summary. The version is
 * LRS_VERSION_ID: the package version, and on a -dev tree also
 * "-g" plus the commit abbreviation. There is no separate version
 * flag; --help is where that string is printed.
 */
static void help(void)
{
    printf(
        "%s v%s\n"
        "Copyright (C) 2026 Alex M (2:203/910)\n"
        "License: GNU General Public License v3 or later\n"
        "https://github.com/glasslike/lrs\n"
        "\n"
        "Lastread sync. Keep lastread cursors synchronized between client and server.\n"
        "\n"
        "usage: lrs sync|show [-c lrs.conf]\n"
        "       lrs --help\n"
        "\n"
        "sync    apply inbound snapshots, add new point areas,\n"
        "        and queue one outbound file (cursors and position requests).\n"
        "        run after the tosser after a session, or after the editor exits\n"
        "show    print the current cursor of each configured area\n"
        "\n"
        "-c FILE            configuration file (default: lrs.conf)\n"
        "-h, --help         show this help and exit\n",
        LRS_NAME, LRS_VERSION_ID);
}

/*
 * -h/--help may sit anywhere on the command line. The word after -c
 * is a path, so it is not read as an option.
 */
static int help_flag(int argc, char **argv)
{
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
            return 1;
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
    Config cfg;
    int rc;

    if (help_flag(argc, argv)) {
        help();
        return 0;
    }
    if (argc < 2) {
        fail_usage("missing command");
        return 2;
    }
    cmd = argv[1];
    if (strcmp(cmd, "selftest") == 0)
        return selftest();
    /* Reject a bad command before the config is opened. Otherwise a
     * leftover -v, or a typo, is reported as a missing lrs.conf. */
    if (strcmp(cmd, "sync") != 0 && strcmp(cmd, "show") != 0) {
        /* -c is a real option. With no path after it, do not call
         * the option unknown. The command still has to come first. */
        if (strcmp(cmd, "-c") == 0 && argc < 3)
            fail_usage("option -c requires a file");
        else
            fail_usage(cmd[0] == '-' ? "unknown option" : "unknown command");
        return 2;
    }
    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0) {
            if (i + 1 >= argc) {
                fail_usage("option -c requires a file");
                return 2;
            }
            cfgpath = argv[++i];
            continue;
        }
        fail_usage("unknown option");
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
    else
        rc = cmd_show(&cfg);
    if (rc != 0)
        log_msg("%s finished with status %d", cmd, rc);
    log_close();
    config_free(&cfg);
    return rc;
}
