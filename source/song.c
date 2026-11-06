#include "song.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int cmp_notes(const void *a, const void *b)
{
    const note_data *x = a, *y = b;
    if (x->t < y->t) return -1;
    if (x->t > y->t) return 1;
    return x->lane - y->lane;
}

static int cmp_events(const void *a, const void *b)
{
    const song_event *x = a, *y = b;
    return x->t < y->t ? -1 : x->t > y->t;
}

static const char *suffix(const char *variation)
{
    return (!variation || !variation[0] || !strcmp(variation, "default")) ? NULL : variation;
}

static void pick_audio(char *out, size_t n, const char *id, const char *base, const char *var)
{
    char rel[160];
    out[0] = 0;
    if (var) {
        snprintf(rel, sizeof(rel), "songs/%s/%s-%s.ogg", id, base, var);
        if (fs_exists(rel)) {
            strncpy(out, rel, n - 1);
            return;
        }
    }
    snprintf(rel, sizeof(rel), "songs/%s/%s.ogg", id, base);
    if (fs_exists(rel)) strncpy(out, rel, n - 1);
}

bool song_load_meta(song *s, const char *id, const char *variation)
{
    memset(s, 0, sizeof(*s));
    strncpy(s->id, id, sizeof(s->id) - 1);
    strncpy(s->variation, variation && variation[0] ? variation : "default", sizeof(s->variation) - 1);
    const char *var = suffix(variation);
    char rel[160];
    if (var) snprintf(rel, sizeof(rel), "songs/%s/%s-metadata-%s.json", id, id, var);
    else snprintf(rel, sizeof(rel), "songs/%s/%s-metadata.json", id, id);
    cJSON *m = fs_json(rel);
    if (!m) return false;
    strncpy(s->name, jstr(m, "songName", id), sizeof(s->name) - 1);
    strncpy(s->artist, jstr(m, "artist", ""), sizeof(s->artist) - 1);
    cJSON *pd = cJSON_GetObjectItem(m, "playData");
    cJSON *ch = cJSON_GetObjectItem(pd, "characters");
    strncpy(s->player, jstr(ch, "player", "bf"), sizeof(s->player) - 1);
    strncpy(s->girlfriend, jstr(ch, "girlfriend", ""), sizeof(s->girlfriend) - 1);
    strncpy(s->opponent, jstr(ch, "opponent", "dad"), sizeof(s->opponent) - 1);
    strncpy(s->stage, jstr(pd, "stage", "mainStage"), sizeof(s->stage) - 1);
    strncpy(s->notestyle, jstr(pd, "noteStyle", "funkin"), sizeof(s->notestyle) - 1);
    cJSON *d;
    cJSON_ArrayForEach(d, cJSON_GetObjectItem(pd, "difficulties"))
    {
        if (cJSON_IsString(d) && s->ndifficulties < 8) strncpy(s->difficulties[s->ndifficulties++], d->valuestring, 15);
    }
    cJSON *off = cJSON_GetObjectItem(m, "offsets");
    s->inst_offset = jnum(off, "instrumental", 0);

    /* audio: Inst[-instrumental or -variation].ogg, Voices-<char>[-variation].ogg */
    const char *instr = jstr(ch, "instrumental", "");
    if (instr[0]) {
        snprintf(rel, sizeof(rel), "songs/%s/Inst-%s.ogg", id, instr);
        if (fs_exists(rel)) strncpy(s->inst_path, rel, sizeof(s->inst_path) - 1);
    }
    if (!s->inst_path[0]) pick_audio(s->inst_path, sizeof(s->inst_path), id, "Inst", var);
    char base[96];
    cJSON *pv = cJSON_GetObjectItem(ch, "playerVocals");
    cJSON *ov = cJSON_GetObjectItem(ch, "opponentVocals");
    const char *pvc = cJSON_IsArray(pv) && cJSON_GetArraySize(pv) > 0 ? cJSON_GetArrayItem(pv, 0)->valuestring : s->player;
    const char *ovc = cJSON_IsArray(ov) && cJSON_GetArraySize(ov) > 0 ? cJSON_GetArrayItem(ov, 0)->valuestring : s->opponent;
    snprintf(base, sizeof(base), "Voices-%s", pvc);
    pick_audio(s->player_voice, sizeof(s->player_voice), id, base, var);
    snprintf(base, sizeof(base), "Voices-%s", ovc);
    pick_audio(s->opp_voice, sizeof(s->opp_voice), id, base, var);
    if (!s->player_voice[0] && !s->opp_voice[0]) pick_audio(s->player_voice, sizeof(s->player_voice), id, "Voices", var);

    /* time changes */
    cJSON *tcs = cJSON_GetObjectItem(m, "timeChanges");
    int n = cJSON_GetArraySize(tcs);
    s->tc = calloc(n > 0 ? n : 1, sizeof(time_change));
    cJSON *t;
    cJSON_ArrayForEach(t, tcs)
    {
        time_change *c = &s->tc[s->ntc];
        c->t = jnum(t, "t", 0);
        c->bpm = jnum(t, "bpm", 100);
        if (c->bpm <= 0) c->bpm = 100;
        if (s->ntc == 0) {
            c->beat = 0;
            c->t = c->t < 0 ? c->t : c->t;
        } else {
            const time_change *p = &s->tc[s->ntc - 1];
            c->beat = cJSON_GetObjectItem(t, "b") ? jnum(t, "b", 0) : p->beat + (c->t - p->t) / (60000.0 / p->bpm);
        }
        s->ntc++;
    }
    if (s->ntc == 0) {
        s->tc[0].bpm = 100;
        s->ntc = 1;
    }
    cJSON_Delete(m);
    return true;
}

bool song_load(song *s, const char *id, const char *variation, const char *difficulty)
{
    if (!song_load_meta(s, id, variation)) return false;
    strncpy(s->difficulty, difficulty, sizeof(s->difficulty) - 1);
    const char *var = suffix(variation);
    char rel[160];
    if (var) snprintf(rel, sizeof(rel), "songs/%s/%s-chart-%s.json", id, id, var);
    else snprintf(rel, sizeof(rel), "songs/%s/%s-chart.json", id, id);
    cJSON *c = fs_json(rel);
    if (!c) return false;
    s->scroll_speed = jnum(cJSON_GetObjectItem(c, "scrollSpeed"), difficulty, 0);
    if (s->scroll_speed <= 0) s->scroll_speed = jnum(cJSON_GetObjectItem(c, "scrollSpeed"), "default", 1.0);
    cJSON *notes = cJSON_GetObjectItem(cJSON_GetObjectItem(c, "notes"), difficulty);
    int n = cJSON_GetArraySize(notes);
    s->notes = calloc(n > 0 ? n : 1, sizeof(note_data));
    cJSON *it;
    cJSON_ArrayForEach(it, notes)
    {
        int d = (int)jnum(it, "d", 0);
        if (d < 0 || d > 7) continue;
        note_data *nd = &s->notes[s->nnotes++];
        nd->t = jnum(it, "t", 0);
        nd->lane = d % 4;
        nd->player = d < 4;
        nd->len = jnum(it, "l", 0);
        if (nd->len < 0) nd->len = 0;
        strncpy(nd->kind, jstr(it, "k", ""), sizeof(nd->kind) - 1);
    }
    qsort(s->notes, s->nnotes, sizeof(note_data), cmp_notes);
    /* drop stacked duplicates (same lane and side within 1 ms) like the chart editor does */
    int w = 0;
    for (int i = 0; i < s->nnotes; i++) {
        if (w > 0) {
            note_data *p = &s->notes[w - 1];
            if (p->lane == s->notes[i].lane && p->player == s->notes[i].player && s->notes[i].t - p->t < 1) continue;
        }
        s->notes[w++] = s->notes[i];
    }
    s->nnotes = w;

    s->events_json = cJSON_DetachItemFromObject(c, "events");
    int ne = cJSON_GetArraySize(s->events_json);
    s->events = calloc(ne > 0 ? ne : 1, sizeof(song_event));
    cJSON_ArrayForEach(it, s->events_json)
    {
        song_event *e = &s->events[s->nevents++];
        e->t = jnum(it, "t", 0);
        strncpy(e->kind, jstr(it, "e", ""), sizeof(e->kind) - 1);
        e->value = cJSON_GetObjectItem(it, "v");
    }
    qsort(s->events, s->nevents, sizeof(song_event), cmp_events);
    cJSON_Delete(c);
    return true;
}

void song_free(song *s)
{
    free(s->tc);
    free(s->notes);
    free(s->events);
    if (s->events_json) cJSON_Delete(s->events_json);
    memset(s, 0, sizeof(*s));
}

static const time_change *tc_at(const song *s, double ms)
{
    const time_change *c = &s->tc[0];
    for (int i = 1; i < s->ntc; i++)
        if (ms >= s->tc[i].t) c = &s->tc[i];
    return c;
}

double song_beat_at(const song *s, double ms)
{
    const time_change *c = tc_at(s, ms);
    return c->beat + (ms - c->t) / (60000.0 / c->bpm);
}

double song_bpm_at(const song *s, double ms) { return tc_at(s, ms)->bpm; }
double song_beat_length(const song *s, double ms) { return 60000.0 / tc_at(s, ms)->bpm; }
double song_step_length(const song *s, double ms) { return 15000.0 / tc_at(s, ms)->bpm; }
