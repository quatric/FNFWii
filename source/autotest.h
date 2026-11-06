/* Scripted runs for testing in Dolphin: when <app>/autotest.txt exists it
 * picks the first scene, presses buttons and saves screenshots at set times.
 *
 *   play bopeebo hard [variation]   start straight into a song
 *   scene title|mainmenu|story|freeplay|options
 *   botplay                         the game plays the notes itself
 *   press 2.5 accept                presses an action at 2.5 s (accept back pause left down up right)
 *   shot 4.0 name                   saves the screen as <app>/shots/name.yuv at 4.0 s
 *   quit 30                         writes <app>/autotest.done and exits at 30 s
 */
#ifndef AUTOTEST_H
#define AUTOTEST_H

#include <stdbool.h>
#include "game.h"

void autotest_init(void);
bool autotest_active(void);
scene_id autotest_first_scene(scene_id def);
void autotest_input(void);
void autotest_frame(void);
void autotest_log_state(const char *fmt, ...);

#endif
