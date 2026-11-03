/* Text-mode UI helpers (80x24 enhanced //e) */
#ifndef UI_H
#define UI_H

#include <stdint.h>
#include "prodos.h"

#define SCR_W 80
#define SCR_H 24

#define ROW_TITLE 0
#define ROW_PANE_TOP 1
#define ROW_LIST 2
#define LIST_ROWS 17
#define ROW_PANE_BOT 19
#define ROW_INFO 20
#define ROW_MSG 21
#define ROW_KEYS 22

void ui_init(void);
void ui_done(void);

void ui_hline(uint8_t x, uint8_t y, uint8_t w, char c);
void ui_box(uint8_t x, uint8_t y, uint8_t w, uint8_t h, const char *title);
void ui_clear_row(uint8_t y);
void ui_text(uint8_t x, uint8_t y, const char *s);
void ui_msg(const char *s);                     /* message row (replaces text) */
void ui_msg2(const char *a, const char *b);

/* Modal dialogs. All restore nothing; caller redraws the screen afterwards. */
void    ui_alert(const char *title, const char *l1, const char *l2);
uint8_t ui_confirm(const char *title, const char *l1, const char *l2);   /* 1 = yes */
uint8_t ui_input(const char *title, const char *prompt, char *buf, uint8_t maxlen); /* 1 = ok */
int8_t  ui_pick_volume(const vol_t *v, uint8_t n, uint8_t cur);          /* index or -1 */

/* Overwrite prompt: 'O' overwrite, 'S' skip, 'A' overwrite all, 'N' skip all, 'C' cancel */
char ui_overwrite(const char *name);

/* Formatting (dst must hold w+1 bytes) */
void fmt_num(char *dst, uint32_t v, uint8_t w);
void fmt_date(char *dst, uint16_t d);           /* 9 chars: DD-MMM-YY or blanks */

#endif
