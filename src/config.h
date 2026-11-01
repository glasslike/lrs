#ifndef LRP_CONFIG_H
#define LRP_CONFIG_H

#include "util.h"

typedef struct CfgArea {
    char tag[LRP_TAG];
    char path[LRP_PATH];
} CfgArea;

typedef struct Config {
    char address[LRP_ADDR];
    char peer[LRP_ADDR];
    int squishuser;
    /* GoldED user name for a JAM .jlr record. Empty: use the only
     * live record. Several records and no name is not guessed. */
    char jamuser[80];
    char inbound[LRP_PATH];
    char outbound[LRP_PATH];
    /* Computed in config_load. Not read from the config file.
     * flo is the normal BSO flow file for "peer"; bsy is its busy flag.
     * Export refuses to rewrite flo while bsy exists, because the mailer
     * is in the middle of reading that flow file. */
    char flo[LRP_PATH];
    char bsy[LRP_PATH];
    char state[LRP_PATH];
    /* Optional. Empty means log_msg writes only to stderr. */
    char log[LRP_PATH];
    /* Directory of Squish or JAM bases named after their echotag.
     * echoarea with only a tag means echodir/tag. Import appends to
     * the config file when a snapshot names a base that is physically
     * here. The format is whichever pair of files is on disk. */
    char echodir[LRP_PATH];
    /* Path of the config file itself, so import can append an echoarea. */
    char file[LRP_PATH];
    /* Set only on the node (config key "server yes"). A cursor this
     * side already has is kept when the peer's snapshot has never
     * applied ours (seen 0). The point leaves this off. It takes that
     * first announcement, and after a server cursor has been applied
     * a later conflict is decided by the clocks. On sync the point
     * records every base found in echodir.
     * The same flag picks the snapshot filename: sNNNNNNN.lrs here,
     * cNNNNNNN.lrs on the point. */
    int server;
    CfgArea *area;
    size_t n;
} Config;

void config_free(Config *cfg);
int config_load(const char *path, Config *cfg);

/* 1 when an area with this tag (case-insensitive) is already loaded. */
int config_has_area(const Config *cfg, const char *tag);

/*
 * Remember a Squish or JAM base that lives in echodir but is not configured yet.
 * Writes one echoarea line into the [areas] block at the end of the
 * config file and adds it in memory. If the header was deleted, the
 * echoarea lines at the end of the file are kept and [areas] is written
 * back in front of them. Blank lines inside that block are removed, so
 * a hand edit does not leave a growing gap.
 * Returns 0 when the line was added, 1 when no base of that tag is in
 * echodir (or both Squish and JAM are present), 2 when the tag is
 * already configured, -1 on error.
 */
int config_adopt_area(Config *cfg, const char *tag);

/* Remove one echoarea line from the config file and from memory.
 * Returns 0, or -1 if the file could not be rewritten. */
int config_forget_area(Config *cfg, const char *tag);

/* Append an echoarea line for every *.sqd or *.jhr in echodir that is not listed
 * yet. The point does this on import so a rescanned base is picked up
 * without being typed in. Returns 0, or -1 if echodir cannot be read. */
int config_discover(Config *cfg);

#endif
