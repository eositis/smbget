/* Applesoft BASIC <-> text conversion. Keywords come from the Applesoft ROM keyword table,
   which is copied into the network scratch buffer (eth_inp) while a conversion runs. */
#ifndef BAS_H
#define BAS_H

#include <stdint.h>

#define BAS_LINE_MAX 239

/* Load the keyword table (512 bytes of scratch memory at tab); returns 0 or 1 if it looks wrong.
   tab needs BAS_SCRATCH bytes. */
#define BAS_SCRATCH 880
uint8_t bas_init(uint8_t *tab);

/* Tokenize one text line (NUL-terminated, no line ending) into out:
   [link lo/hi placeholder][line no lo/hi][tokens][0]. Returns the length, 0 if the line has no
   line number (blank lines are skipped). out must hold BAS_LINE_MAX + 8 bytes. */
uint16_t bas_tok_line(const char *line, uint8_t *out);

/* List one tokenized line body (tokens after the 4-byte header, up to the 0) as text
   "10  PRINT "HI"" without a line ending. Returns the text length, or 0xFFFF if room ran out. */
uint16_t bas_detok_line(uint16_t lineno, const uint8_t *body, char *out, uint16_t room);

#endif
