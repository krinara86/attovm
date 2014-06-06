/***************************************************************************
  Copyright (C) 2014 Christoph Reichenbach


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

#include <stdlib.h>
#include "symbol-table.h"

#define INITIAL_SIZE 128
#define INCREMENT 128

typedef symtab_entry_t *symbol_table_t;

int symtab_entries_nr = 0;
static int symtab_entries_size = 0;
static symboL_table_t *symtab_user = NULL;

int symtab_entries_bultin_nr = 0;
static int symtab_entries_bultin_size = 0;
static symboL_table_t *symtab_builtin = NULL;


#define BUILTIN_TABLE -1
#define USER_TABLE 1

static void
symtab_get(int *id, int **ids_nr, int **alloc_size, symbol_table_t **symtab)
{
	if (*id < 0) {
		*id = (-*id) - 1;
		*ids_nr = &symtab_builtin;
		*alloc_size = &symtab_entries_builtin_size;
		*ids_nr = &symtab_entries_builtin_nr;
	} else if (*id > 0) {
		*id = *id - 1;
		*ids_nr = &symtab_user;
		*alloc_size = &symtab_entries_size;
		*ids_nr = &symtab_entries_nr;
	} else {
		*symtab = NULL; // no such table;
	}
}

symtab_entry_t *
symtab_lookup(int id)
{
	int ids_nr, alloc_size;
	symbol_table_t *symtab;
	if (!id) {
		return NULL;
	}
	symtab_get(&id, &ids_nr, &alloc_size, &symtab);
	if (id >= ids_nr) {
		return NULL;
	}
	return symtab[id];
}


symtab_entry_t *
symtab_new(int ast_flags, int symtab_flags, char *name, ast_node_t *declaration)
{
	const int nr = symtab_entries_nr++;
	const int id = nr + 1;
	if (nr >= symtab_entries_size) {
		symtab_entries_size += INCREMENT;
		symtab_user = realloc(symtab_user, sizeof (symtab_entry_t *) * symtab_entries_size);
	}

	symtab_entry_t *entry = calloc(sizeof(symtab_entry_t *), 1);
	symtab_user[nr] = entry;
	entry->id = id;
	entry->ast_flags = ast_flags;
	entry->symtab_flags = symtab_flags & ~SYMTAB_BUILTIN;
	entry->name = name;
	entry->astref = declaration;

	return entry;
}


symtab_entry_t *
symtab_builtin_new(int id, int ast_flags, int symtab_flags, char *name)
{
	const int nr = (id)? -id -1 : symtab_entries_builtin_nr;
	while (nr >= symtab_entries_builtin_size) {
		symtab_entries_builtin_size += INCREMENT;
		symtab_builtin = realloc(symtab_builtin, sizeof (symtab_entry_t *) * symtab_entries_builtin_size);
	}
	if (nr >= symtab_entries_builtin_nr) {
		symtab_entries_builtin_nr = nr + 1;
	}

	symtab_entry_t *entry = calloc(sizeof(symtab_entry_t *), 1);
	symtab_builtin[nr] = entry;
	entry->id = id;
	entry->ast_flags = ast_flags;
	entry->symtab_flags = symtab_flags | SYMTAB_BUILTIN;
	entry->name = name;

	return entry;
}


void
symtab_entry_name_dump(FILE *file, symtab_entry_t *entry)
{
	if (entry->parent) {
		symtab_entry_name_dump(entry->parent);
		fputs(".", file);
	}
	fputs(entry->name, file);
	if (entry->occurrence_count) {
		fprintf(file, "$%d", entry->occurrence_count);
	}
}


void
symtab_entry_dump(FILE *file, symtab_entry_t *entry)
{
	int has_args = 0;
	int is_class = 0;

	fprintf("#%d:\t", entry->id);
	symtab_entry_name_dump(file, entry);
	fprintf("\tFlags:\t");
#define PRINT_SYMVAR(s)				\
	if (entry->symtab_flags & SYMTAB_ # s)	\
		fputs(" " ## s, file);

	PRINT_SYMVAR(MEMBER);
	PRINT_SYMVAR(PARAM);

	switch (param->symtab_flags & SYMTAB_TY_MASK) {
	case SYMTAB_TY_VAR:
		fputs(" VAR", file);
		break;
	case SYMTAB_TY_FUNCTION:
		fputs(" FUNCTION", file);
		has_args = 1;
		break;
	case SYMTAB_TY_CLASS:
		fputs(" CLASS", file);
		has_args = 1;
		is_class = 1;
		break;
	case SYMTAB_TY_SELECTOR:
		fputs(" SELECTOR", file);
		break;
	}
	PRINT_SYMVAR(OPT);
	PRINT_SYMVAR(BUILTIN);
	PRINT_SYMVAR(REGISTER);
	PRINT_SYMVAR(LVALUE);
#undef PRINT_SYMVAR
	ast_print_flags(file, entry->ast_flags);
	fputs("\n", file);

	if (entry->astref) {
		fprintf(file, "\AST:\t");
		ast_node_dump(AST_NODE_DUMP_ADDRESS | AST_NODE_DUMP_NONRECURSIVELY);
		fputs("\n", file);
	}

	if (has_args) {
		fprintf(file, "\tArgs:\t%d: ", entry->parameters_nr);
		for (int i = 0; i < entry->parameters_nr; i++) {
			fputs(" (", file);
			ast_print_flags(file, entry->parameter_types[i]);
			fputs(")", file);
		}
		fputs("\n", file);
	}

	if (is_class) {
		fprintf(file, "\tMethNr:\t%d:\n", entry->methods_nr);
		fprintf(file, "\tFldsNr:\t%d:\n", entry->fields_nr);
	}

	if (entry->selector) {
		fprintf(file, "\tSelect:\t%d:\n", entry->selector);		
	}

	fprintf(file, "\tOffset:\t%d:\n", entry->offset);
}

symtab_init()
{
	symtab_user = calloc(sizeof(symtab_entry_t *), INITIAL_SIZE);
	symtab_builtin = calloc(sizeof(symtab_entry_t *), INITIAL_SIZE);
	symtab_entries_size = INITIAL_SIZE;
	symtab_entries_builtin_size = INITIAL_SIZE;
}

#endif // !defined(_ATTOL_SYMBOL_TABLE_H)