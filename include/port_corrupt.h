/* Corrupt Patrol's corrupted fighters: a purple aura that stays lit and a purple glow over the body
   (port/src/port_corrupt.c). Sides are 0 (left, the player's) and 1. */
#ifndef PORT_CORRUPT_H
#define PORT_CORRUPT_H
void PortCorrupt_SetNext(int mask);  /* the sides corrupted in the next battles: bit 0 left, bit 1 right */
void PortCorrupt_Set(int side, int on);
int PortCorrupt_Get(int side);       /* 1 while a battle is up and the side is corrupted */
void PortCorrupt_BattleStart(void);
void PortCorrupt_BattleEnd(void);
void PortCorrupt_Tick(void);
int PortCorrupt_Glow(void);          /* the body glow's alpha this frame */
#endif
