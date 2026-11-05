/* PC only: costume choices of more than four (include/port_chara.h: PORT_COSTUME_MAX; src/menu/port_costume.c).
 *
 * The select screens' movies have four costume plates (mc_color_plate_1..4, their numbers from a picture of four).
 * A character with more costumes (a package's: up to 16) is chosen from pages of four: the plates show the page the
 * cursor is on (left / right go on through the pages, as through the four before), the numbers of the pages after the
 * first printed over the plates with the game's font, an arrow where there is a page before or after. Square on the
 * costume plates opens the costume preview (PortCostume_Pick): the character's 3D model in each of its costumes. */
#ifndef PORT_COSTUME_H
#define PORT_COSTUME_H

#define PORT_PLATE_FIRST(c) ((c) / 4 * 4)                                               /* the page's first costume */
#define PORT_PLATE_COUNT(c, n) ((n) - PORT_PLATE_FIRST(c) < 4 ? (n) - PORT_PLATE_FIRST(c) : 4) /* its plates */
#define PORT_PLATE_NO(c) ((c) % 4 + 1)                                                  /* the plate of costume c */

/* The draw of plate j (0..3) of movie `flash` (`plate`: its clip, "mc_color_plate_<j + 1>"), with the cursor on
   costume `costume` of `count`: its number and the arrows of the pages. Call it where the plate is laid out. */
void PortPlate_Text(MFlash *flash, char *plate, s32 j, s32 costume, s32 count);
/* What PortPlate_Text laid out for movie `flash`, printed: call it right after that movie is drawn. */
void PortPlate_Draw(MFlash *flash);
/* The costume preview, a screen of its own for pad `padNo`: character `chara` (`count` costumes) in 3D in the costume
   under the cursor, which starts on *costume. 1 with *costume the one picked (cross), 0 back (triangle, circle). */
s32 PortCostume_Pick(s32 padNo, s32 chara, s32 count, s32 *costume);
/* Whether the preview may open now: a character, more than one costume, not online. */
s32 PortCostume_CanPick(s32 chara, s32 count);

#endif
