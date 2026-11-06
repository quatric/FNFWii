/* Songs in the game's v2 chart format: <id>-metadata[-variation].json and
 * <id>-chart[-variation].json, plus the conductor (BPM changes). */
#ifndef SONG_H
#define SONG_H

#include <stdbool.h>
#include "util.h"

typedef struct {
    double t;      /* ms */
    double beat;   /* beat at t */
    double bpm;
} time_change;

typedef struct {
    double t;
    int lane;       /* 0..3 */
    bool player;    /* must-hit (player's) note */
    double len;     /* sustain, ms */
    char kind[32];
} note_data;

typedef struct {
    double t;
    char kind[32];
    cJSON *value;   /* owned by events_json */
} song_event;

typedef struct {
    char id[48];
    char variation[16];
    char difficulty[16];
    char name[64];
    char artist[64];
    char player[48], girlfriend[48], opponent[48];
    char stage[48];
    char notestyle[24];
    char inst_path[160];
    char player_voice[160], opp_voice[160];
    double inst_offset;
    double scroll_speed;
    time_change *tc;
    int ntc;
    note_data *notes;
    int nnotes;
    song_event *events;
    int nevents;
    cJSON *events_json;
    char difficulties[8][16];
    int ndifficulties;
    double length_ms;
} song;

/* Loads only the metadata (names, difficulties) of a variation. */
bool song_load_meta(song *s, const char *id, const char *variation);
bool song_load(song *s, const char *id, const char *variation, const char *difficulty);
void song_free(song *s);

/* Conductor */
double song_beat_at(const song *s, double ms);
double song_bpm_at(const song *s, double ms);
static inline double song_step_at(const song *s, double ms) { return song_beat_at(s, ms) * 4; }
double song_beat_length(const song *s, double ms);   /* ms per beat */
double song_step_length(const song *s, double ms);

#endif
