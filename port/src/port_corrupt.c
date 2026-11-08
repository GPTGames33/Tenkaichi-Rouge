/* Corrupt Patrol's corrupted fighters (docs/port/corrupt_patrol.md): a fighter of the battle shown amplified by the
   purple power - an aura that never goes out, in violet and magenta (src/battle/eft_n.c), and a purple glow pulsing
   over the body's colours, drawn the way the Kaioken's red is (src/sys/gfxm_d_c.c). Looks only: nothing of the fight
   changes here (the mode makes the corrupted stronger with Z items and the AI's level).

   Which sides are corrupted is set before a battle (PortCorrupt_SetNext, the mode's screens) and taken when the
   battle's effects start (PortCorrupt_BattleStart, eft_n.c's EftAuraMgr_Init), so "Fight Again" keeps it;
   BT3_CORRUPT=<side mask> (1 the left side, 2 the right, 3 both) for tests. Outside a battle nobody is. */
#include <stdlib.h>
#include "port_corrupt.h"

static int sOn[2];
static int sNext;   /* the next battles' mask */
static int sActive; /* a battle's effects are up */
static unsigned sFrame;

void PortCorrupt_SetNext(int mask) {
    sNext = mask & 3;
}
void PortCorrupt_Set(int side, int on) {
    if (side >= 0 && side < 2) {
        sOn[side] = on != 0;
    }
}
int PortCorrupt_Get(int side) {
    return sActive && side >= 0 && side < 2 && sOn[side];
}
void PortCorrupt_BattleStart(void) {
    const char *e = getenv("BT3_CORRUPT");
    int m = e != NULL ? atoi(e) : sNext;

    sOn[0] = m & 1;
    sOn[1] = (m >> 1) & 1;
    sActive = 1;
}
void PortCorrupt_BattleEnd(void) {
    sActive = 0;
}
void PortCorrupt_Tick(void) {
    sFrame++;
}
/* The body glow's strength this frame: a slow pulse, 18..54 of the 128 the palette pass takes (the Kaioken's red
   goes to 70) */
int PortCorrupt_Glow(void) {
    int t = (int)(sFrame % 96);
    int tri = t < 48 ? t : 96 - t;
    return 18 + tri * 3 / 4;
}
