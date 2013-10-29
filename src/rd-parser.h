/***************************************************************************
  Copyright (C) 2013 Christoph Reichenbach


 This program may be modified and copied freely according to the terms of
 the GNU general public license (GPL), as long as the above copyright
 notice and the licensing information contained herein are preserved.

 Please refer to www.gnu.org for licensing details.

 This work is provided AS IS, without warranty of any kind, expressed or
 implied, including but not limited to the warranties of merchantability,
 noninfringement, and fitness for a specific purpose. The author will not
 be held liable for any damage caused by this work or derivatives of it.

 By using this source code, you agree to the licensing terms as stated
 above.


 Please contact the maintainer for bug reports or inquiries.

 Current Maintainer:

    Christoph Reichenbach (CR) <creichen@gmail.com>

***************************************************************************/

// Recursive-Descent-Parser: Header-Datei, manuell erstellt

#ifndef _CMINOR_SRC_RD_PARSER_H
#define _CMINOR_SRC_RD_PARSER_H

#include <stdlib.h>

#include "ast.h"

// Konstanten für Terminale
enum {
  STRING = 0x102,
  NAME,
  UINT,
  INT,
  REAL,
  BOOL,
  CHAR,
};

typedef union {
	ast_node_t *node;
	unsigned long long ull;
	signed long long sll;
	double real;
	char c;
	char *str;
} yylval_t;

extern yylval_t yylval;

#endif // !defined(_CMINOR_SRC_RD_PARSER_H)
