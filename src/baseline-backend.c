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

#include <string.h>
#include <assert.h>
#include <stddef.h>
#include <stdbool.h>

#include "ast.h"
#include "baseline-backend.h"
#include "assembler.h"
#include "registers.h"
#include "class.h"
#include "object.h"
#include "errors.h"


#define VAR_IS_OBJ

#define FAIL(...) { fprintf(stderr, "[baseline-backend] L%d: Compilation failed:", __LINE__); fprintf(stderr, __VA_ARGS__); exit(1); }

typedef struct relative_jump_label_list {
	relative_jump_label_t label;
	struct relative_jump_label_list *next;
} relative_jump_label_list_t;

// Uebersetzungskontext
typedef struct {
	// Fuer den aktuellen Ausfuehrungsrahmen:  Welches Register indiziert temporaere Variablen?
	int temp_reg_base;

	// Falls verfuegbar/in Schleife: Sprungmarken 
	relative_jump_label_list_t *continue_labels, *break_labels;
} context_t;


static void
context_copy(context_t *dest, context_t *src)
{
	memcpy(dest, src, sizeof(context_t));
}

static relative_jump_label_t *
jll_add_label(relative_jump_label_list_t **list)
{
	relative_jump_label_list_t *node = (relative_jump_label_list_t *) malloc(sizeof (relative_jump_label_list_t));
	node->next = *list;
	*list = node;
	return &node->label;
}

static void
jll_labels_resolve(relative_jump_label_list_t **list, void *addr)
{
	while (*list) {
		relative_jump_label_list_t *node = *list;
		*list = node->next;
		buffer_setlabel(&node->label, addr);
		free(node);
	}
}

static void
baseline_compile_expr(buffer_t *buf, ast_node_t *ast, int dest_register, context_t *context);

long long int builtin_op_obj_test_eq(object_t *a0, object_t *a1);


// Gibt Groesse des Stapelrahmens (in Bytes) zurueck
static int
baseline_compile_prepare_arguments(buffer_t *buf, int children_nr, ast_node_t **children, context_t *context);


static void
fail_at_node(ast_node_t *node, char *msg)
{
	fprintf(stderr, "Fatal: %s", msg);
	if (node->source_line) {
		fprintf(stderr, " in line %d\n", node->source_line);
	} else {
		fprintf(stderr, "\n");
	}
	ast_node_dump(stderr, node, AST_NODE_DUMP_FORMATTED | AST_NODE_DUMP_ADDRESS | AST_NODE_DUMP_FLAGS);
	fprintf(stderr, "\n");
	fail("execution halted on error");
}

// Kann dieser Knoten ohne temporaere Register berechnet werden?
static int
is_simple(ast_node_t *n)
{
	return IS_VALUE_NODE(n);
}

static void
emit_optmove(buffer_t *buf, int dest, int src)
{
	if (dest != src) {
		emit_move(buf, dest, src);
	}
}

// Load Address
static void
emit_la(buffer_t *buf, int reg, void *p)
{
	emit_li(buf, reg, (long long) p);
}

static void
emit_fail_at_node(buffer_t *buf, ast_node_t *node, char *msg)
{
	emit_la(buf, REGISTER_A0, node);
	emit_la(buf, REGISTER_A1, msg);
	emit_la(buf, REGISTER_V0, &fail_at_node);
	emit_jalr(buf, REGISTER_V0);
}



static void
baseline_compile_builtin_convert(buffer_t *buf, ast_node_t *arg, int to_ty, int from_ty, int dest_register, context_t *context)
{
	// Annahme: zu konvertierendes Objekt ist in a0

#ifdef VAR_IS_OBJ
	if (to_ty == TYPE_VAR) {
		to_ty = TYPE_OBJ;
	}
	if (from_ty == TYPE_VAR) {
		from_ty = TYPE_OBJ;
	}
#endif

	switch (from_ty) {
	case TYPE_INT:
		switch (to_ty) {
		case TYPE_INT:
			return;
		case TYPE_OBJ:
			emit_la(buf, REGISTER_V0, &new_int);
			emit_jalr(buf, REGISTER_V0);
			emit_optmove(buf, dest_register, REGISTER_V0);
			return;
		case TYPE_VAR:
			FAIL("VAR not supported");
			return;
		}
		break;
	case TYPE_OBJ:
		switch (to_ty) {
		case TYPE_INT: {
			emit_ld(buf, REGISTER_T0, REGISTER_A0, 0);
			emit_la(buf, REGISTER_V0, &class_boxed_int);
			relative_jump_label_t jump_label;
			// Falls Integer-Objekt: Springe zur Dekodierung
			emit_beq(buf, REGISTER_T0, REGISTER_V0, &jump_label);
			emit_fail_at_node(buf, arg, "attempted to convert non-Integer object to int");
			// Erfolgreiche Dekodierung:
			buffer_setlabel2(&jump_label, buf);
			emit_ld(buf, dest_register, REGISTER_A0,
				// Int-Wert im Objekt:
				offsetof(object_t, members[0].int_v));
			return;
		}
		case TYPE_OBJ:
			return;
		case TYPE_VAR:
			FAIL("VAR not supported");
			return;
		}
		break;
	case TYPE_VAR:
		switch (to_ty) {
		case TYPE_INT:
			FAIL("VAR not supported");
			return;
		case TYPE_OBJ:
			FAIL("VAR not supported");
			return;
		case TYPE_VAR:
			return;
		}
		break;
	}
	FAIL("Unsupported conversion: %x to %x\n", from_ty, to_ty);
}

static void
baseline_compile_builtin_eq(buffer_t *buf, int dest_register,
			    int a0, int a0_ty,
			    int a1, int a1_ty)
{
	//#ifdef VAR_IS_OBJ
	if (a0_ty == TYPE_VAR) {
		a0_ty = TYPE_OBJ;
	}
	if (a1_ty == TYPE_VAR) {
		a1_ty = TYPE_OBJ;
	}
	//#endif

	bool temp_object = false;
	if (a0_ty != a1_ty) {
		// Int -> Object
		if (a0_ty == TYPE_INT || a1_ty == TYPE_INT) {
			// Einer der Typen muss nach Obj konvertiert werden
			int conv_reg;
			if (a0_ty == TYPE_INT) {
				conv_reg = a0;
				a0_ty = TYPE_OBJ;
			} else {
				conv_reg = a1;
				a1_ty = TYPE_OBJ;
			}
			temp_object = true;
			// Temporaeres Objekt auf dem Stapel erzeugen
			emit_push(buf, conv_reg);
			emit_la(buf, conv_reg, &class_boxed_int);
			emit_push(buf, conv_reg);
			emit_move(buf, conv_reg, REGISTER_SP);
		}

		// Noch nicht implementiert
		assert(a0_ty != TYPE_VAR && a1_ty != TYPE_VAR);
	}

	// Beide Typen sind nun gleich
	switch (a0_ty) {

	case TYPE_INT:
		emit_seq(buf, dest_register, a0, a1);
		break;

	case TYPE_OBJ:
		emit_la(buf, REGISTER_V0, builtin_op_obj_test_eq);
		emit_jalr(buf, REGISTER_V0);
		break;

	case TYPE_VAR:
	default:
		 FAIL("Unsupported type equality check: %x\n", a0_ty);
	}

	if (temp_object) {
		// Temporaeres Objekt vom Stapel deallozieren
		emit_addiu(buf, REGISTER_SP, 16);
	}

}


static void
baseline_compile_builtin_op(buffer_t *buf, int result_ty, int op, ast_node_t **args, int dest_register, context_t *context)
{
	// Bestimme Anzahl der Parameter
	int args_nr = 2;

	switch (op) {
	case BUILTIN_OP_CONVERT:
	case BUILTIN_OP_NOT:
	case BUILTIN_OP_ALLOCATE:
		args_nr = 1;
		break;

	case BUILTIN_OP_DIV:
		args_nr = 0;
		break;
	}

	// Parameter laden
	if (args_nr) {
		baseline_compile_prepare_arguments(buf, args_nr, args, context);
	}

	switch (op) {
	case BUILTIN_OP_ADD:
		if (dest_register == REGISTER_A0) {
			emit_add(buf, REGISTER_A0, REGISTER_A1);
		} else {
			emit_add(buf, REGISTER_A1, REGISTER_A0);
			emit_optmove(buf, dest_register, REGISTER_A1);
		}
		break;

	case BUILTIN_OP_SUB:
		emit_sub(buf, REGISTER_A0, REGISTER_A1);
		emit_optmove(buf, dest_register, REGISTER_A0);
		break;

	case BUILTIN_OP_NOT:
		emit_not(buf, dest_register, REGISTER_A0);
		break;

	case BUILTIN_OP_TEST_EQ:
		baseline_compile_builtin_eq(buf, dest_register,
					    REGISTER_A0, args[0]->type & TYPE_FLAGS,
					    REGISTER_A1, args[1]->type & TYPE_FLAGS);
		break;

	case BUILTIN_OP_TEST_LE:
		emit_sle(buf, dest_register, REGISTER_A0, REGISTER_A1);
		break;

	case BUILTIN_OP_TEST_LT:
		emit_slt(buf, dest_register, REGISTER_A0, REGISTER_A1);
		break;

	case BUILTIN_OP_DIV:
		baseline_compile_expr(buf, args[0], REGISTER_V0, context);
		baseline_compile_expr(buf, args[1], REGISTER_T0, context);
		emit_li(buf, REGISTER_A2, 0); // muss vor Division auf 0 gesetzt werden
		emit_div_a2v0(buf, registers_temp[0]);
		emit_optmove(buf, dest_register, REGISTER_V0); // Ergebnis in $v0
		break;

	case BUILTIN_OP_MUL:
		if (dest_register == REGISTER_A0) {
			emit_mul(buf, REGISTER_A0, REGISTER_A1);
		} else {
			emit_mul(buf, REGISTER_A1, REGISTER_A0);
			emit_optmove(buf, dest_register, REGISTER_A1);
		}
		break;

	case BUILTIN_OP_CONVERT:
		baseline_compile_builtin_convert(buf, args[0], result_ty, args[0]->type & TYPE_FLAGS, dest_register, context);
		break;

	default:
		FAIL("Unsupported builtin op: %d\n", op);
	}
}


// Gibt Groesse des Stapelrahmens (in Bytes) zurueck
static int
baseline_compile_prepare_arguments(buffer_t *buf, int children_nr, ast_node_t **children, context_t *context)
{
	// Optimierung:  letzter nichtrivialer Parameter muss nicht zwischengesichert werden...
	int start_of_trailing_simple = children_nr;
	for (int i = children_nr - 1; i >= 0; i--) {
		if (is_simple(children[i])) {
			start_of_trailing_simple = i;
		} else break;
	}

	int last_nonsimple = start_of_trailing_simple - 1;
	if (last_nonsimple >= REGISTERS_ARGUMENT_NR) {
		// ... Ausnahme: wenn der letzte nichttriviale Parameter sowieso
		// nicht in Register gehoert, nutzt diese Optimierung nichts
		last_nonsimple = -1;
	}
	// Ende Vorbereitung der Optimierung [last_nonsimple]

	// Stapelrahmen vorbereiten, soweit noetig
	int stack_frame_size = 0;
	if (children_nr > REGISTERS_ARGUMENT_NR) {
		stack_frame_size = (children_nr - REGISTERS_ARGUMENT_NR);
		if (stack_frame_size & 1) {
			++stack_frame_size; // 16-Byte-Ausrichtung, gem. AMD64-ABI
		}

		emit_addiu(buf, REGISTER_SP, -(stack_frame_size * 8));
	}

	// Nichttriviale Werte und Werte, die ohnehin auf den Stapel muessen, rekursiv ausrechnen
	for (int i = 0; i < children_nr; i++) {
		int reg = REGISTER_V0;
		
		if (i > REGISTERS_ARGUMENT_NR || !is_simple(children[i])) {
			if (i == last_nonsimple) { // Opt: [non_simple]
				reg = registers_argument[i];
			}
			baseline_compile_expr(buf, children[i], reg, context);

			if (i >=  REGISTERS_ARGUMENT_NR) {
				// In Ziel speichern
				emit_sd(buf, reg, REGISTER_SP, 8 * (i - REGISTERS_ARGUMENT_NR));
			} else if (i != last_nonsimple) {
				// Muss in temporaerem Speicher ablegen
				emit_sd(buf, reg, context->temp_reg_base, children[i]->storage * 8);
			}
		}
	}

	// Triviale Registerinhalte generieren, vorher berechnete Werte bei Bedarf laden
	for (int i = 0; i < children_nr; i++) {
		if (is_simple(children[i])) {
			if (i >= REGISTERS_ARGUMENT_NR) {
				baseline_compile_expr(buf, children[i], REGISTER_V0, context);
				emit_sd(buf, REGISTER_V0, REGISTER_SP, 8 * (i - REGISTERS_ARGUMENT_NR));
			} else {
				baseline_compile_expr(buf, children[i], registers_argument[i], context);
			}
		} else if (i < REGISTERS_ARGUMENT_NR && i != last_nonsimple) {
			emit_ld(buf, registers_argument[i], context->temp_reg_base, children[i]->storage * 8);
		}
	}

	return stack_frame_size * 8;
}

// Der Aufrufer speichert; der Aufgerufene haelt sich immer an dest_register
static void
baseline_compile_expr(buffer_t *buf, ast_node_t *ast, int dest_register, context_t *context)
{
	switch (NODE_TY(ast)) {
	case AST_VALUE_INT:
		emit_li(buf, dest_register, AV_INT(ast));
		break;

	case AST_VALUE_STRING:
		emit_la(buf, dest_register, new_string(AV_STRING(ast), strlen(AV_STRING(ast))));
		break;

	case AST_VALUE_ID: {
		int reg;
		symtab_entry_t *sym = ast->sym;
		const int offset = 8 * sym->offset;
		if (SYMTAB_IS_STATIC(sym)) {
			reg = REGISTER_GP;
		} else if (SYMTAB_IS_STACK_DYNAMIC(sym)) {
			reg = REGISTER_FP;
		} else {
			fprintf(stderr, "Don't know how to load this variable:");
			symtab_entry_dump(stderr, sym);
			FAIL("Unable to load variable");
		}
		if (ast->type & AST_FLAG_LVALUE) {
			// Wir wollen nur die Adresse:
			emit_li(buf, dest_register, offset);
			emit_add(buf, dest_register, reg);
		} else {
			emit_ld(buf, dest_register, reg, offset);
		}
	}
		break;

	case AST_NODE_VARDECL:
		if (ast->children[1] == NULL) {
			// Keine Initialisierung?
			break;
		}
		// fall through
	case AST_NODE_ASSIGN:
		baseline_compile_expr(buf, ast->children[0], REGISTER_V0, context);
		emit_push(buf, REGISTER_V0);
		baseline_compile_expr(buf, ast->children[1], REGISTER_V0, context);
		emit_pop(buf, REGISTER_T0);
		emit_sd(buf, REGISTER_V0, REGISTER_T0, 0);
		break;

	case AST_NODE_ARRAYVAL: {
		if (ast->children[1]) {
			// Laden mit expliziter Groessenangabe
			baseline_compile_expr(buf, ast->children[1], REGISTER_A0, context);
			emit_li(buf, REGISTER_T0, ast->children[0]->children_nr);
			relative_jump_label_t jl;
			emit_ble(buf, REGISTER_T0, REGISTER_A0, &jl);
			emit_fail_at_node(buf, ast, "Requested array size is smaller than number of array elements");
			buffer_setlabel2(&jl, buf);
		} else {
			// Laden mit impliziter Groesse
			emit_li(buf, REGISTER_A0, ast->children[0]->children_nr);
		}
		emit_la(buf, REGISTER_V0, &new_array);
		emit_jalr(buf, REGISTER_V0);
		// We now have the allocated array in REGISTER_V0
		emit_push(buf, REGISTER_V0);
		for (int i = 0; i < ast->children[0]->children_nr; i++) {
			ast_node_t *child = ast->children[0]->children[i];
			baseline_compile_expr(buf, child, REGISTER_T0, context);
			if (!is_simple(child)) {
				emit_ld(buf, REGISTER_V0, REGISTER_SP, 0);
			}
			emit_sd(buf, REGISTER_T0, REGISTER_V0, 16 /* header + groesse */ + 8 * i);
		}
		emit_pop(buf, dest_register);
	}
		break;

	case AST_NODE_ARRAYSUB: {
		baseline_compile_expr(buf, ast->children[0], REGISTER_V0, context);
		emit_la(buf, REGISTER_T1, &class_array);
		emit_ld(buf, REGISTER_T0, REGISTER_V0, 0);

		relative_jump_label_t jl;
		emit_beq(buf, REGISTER_T0, REGISTER_T1, &jl);
		emit_fail_at_node(buf, ast, "Attempted to index non-array");
		buffer_setlabel2(&jl, buf);

		emit_push(buf, REGISTER_V0);
		baseline_compile_expr(buf, ast->children[1], REGISTER_T0, context);
		emit_pop(buf, REGISTER_V0);
		emit_ld(buf, REGISTER_T1, REGISTER_V0, 8);
		// v0: Array
		// t0: offset
		// t1: size
		
		emit_bgez(buf, REGISTER_T0, &jl);
		emit_fail_at_node(buf, ast, "Negative index into array");
		buffer_setlabel2(&jl, buf);

		emit_blt(buf, REGISTER_T0, REGISTER_T1, &jl);
		emit_fail_at_node(buf, ast, "Index into array out of bounds");
		buffer_setlabel2(&jl, buf);

		emit_slli(buf, REGISTER_T0, REGISTER_T0, 3);
		emit_add(buf, REGISTER_V0, REGISTER_T0);

		// index is valid
		if (ast->type & AST_FLAG_LVALUE) {
			emit_addiu(buf, REGISTER_V0, 16); // +16 um ueber Typ-ID und Arraygroesse zu springen
			emit_optmove(buf, dest_register, REGISTER_V0);
		} else {
			emit_ld(buf, dest_register, REGISTER_V0, 16);
		}
	}
		break;

	case AST_NODE_IF: {
		baseline_compile_expr(buf, ast->children[0], REGISTER_V0, context);
		relative_jump_label_t false_label, end_label;
		emit_beqz(buf, REGISTER_V0, &false_label);
		baseline_compile_expr(buf, ast->children[1], REGISTER_V0, context);
		if (ast->children[2]) {
			emit_j(buf, &end_label);
			buffer_setlabel2(&false_label, buf);
			baseline_compile_expr(buf, ast->children[2], REGISTER_V0, context);
			buffer_setlabel2(&end_label, buf);
		} else {
			buffer_setlabel2(&false_label, buf);
		}
		break;
	}

	case AST_NODE_WHILE: {
		relative_jump_label_t loop_label, exit_label;
		void *loop_target = buffer_target(buf);

		// Schleife beendet?
		baseline_compile_expr(buf, ast->children[0], REGISTER_V0, context);
		emit_beqz(buf, REGISTER_V0, &exit_label);

		// Schleifenkoerper
		context_t context_backup;  // Kontext sichern (s.u.)
		context_copy(&context_backup, context);
		context->continue_labels = NULL;
		context->break_labels = NULL;
		baseline_compile_expr(buf, ast->children[1], REGISTER_V0, context);
		emit_j(buf, &loop_label);

		// Schleifenende, und Sprungmarken einsetzen
		void *exit_target = buffer_target(buf);
		buffer_setlabel(&loop_label, loop_target);
		buffer_setlabel(&exit_label, exit_target);
		// Break-Continue-Sprungmarken binden
		jll_labels_resolve(&context->continue_labels, loop_target);
		jll_labels_resolve(&context->break_labels, exit_target);

		// Kontext wiederherstellen, damit umgebende Schleifen wieder Zugriff auf ihre `continue_label' und `break_label' erhalten
		context_copy(context, &context_backup);
	}
		break;

	case AST_NODE_CONTINUE:
		// Sprungbefehl fuer spaeter merken
		emit_j(buf, jll_add_label(&context->continue_labels));
		break;

	case AST_NODE_BREAK:
		// Sprungbefehl fuer spaeter merken
		emit_j(buf, jll_add_label(&context->break_labels));
		break;

	case AST_NODE_FUNAPP: {
		// Annahme: Funktionsaufrufe (noch keine Unterstuetzung fuer Selektoren)
		symtab_entry_t *sym = ast->children[0]->sym;
		assert(sym);
		// Besondere eingebaute Operationen werden in einer separaten Funktion behandelt
		if (sym->id < 0 && sym->symtab_flags & SYMTAB_HIDDEN) {
			baseline_compile_builtin_op(buf, ast->type & TYPE_FLAGS, sym->id,
						    ast->children[1]->children, dest_register, context);
		} else {
			// Normaler Funktionsaufruf
			// Argumente laden
			int stack_frame_size = baseline_compile_prepare_arguments(buf, ast->children[1]->children_nr, ast->children[1]->children, context);

			if (!sym->r_mem) {
				fail_at_node(ast, "No code known");
			}
			assert(sym->r_mem); // Sprungadresse muss bekannt sein
			emit_la(buf, REGISTER_V0, sym->r_mem);
			emit_jalr(buf, REGISTER_V0);

			// Stapelrahmen nachbereiten, soweit noetig
			if (stack_frame_size) {
				emit_addiu(buf, REGISTER_SP, stack_frame_size);
			}
		}
	}
		break;

	case AST_NODE_BLOCK:
		for (int i = 0; i < ast->children_nr; i++) {
			baseline_compile_expr(buf, ast->children[i], dest_register, context);
		}
		break;

	default:
		ast_node_dump(stderr, ast, 0);
		fail("Unsupported AST fragment");
	}
}

void *builtin_op_print(object_t *arg);  // builtins.c

buffer_t
baseline_compile(ast_node_t *root,
		 void *static_memory)
{
	context_t context;
	context.temp_reg_base = REGISTER_GP;
	context.continue_labels = NULL;
	context.break_labels = NULL;

	buffer_t buf = buffer_new(1024);
	emit_push(&buf, REGISTER_GP);
	emit_la(&buf, REGISTER_GP, static_memory);
	baseline_compile_expr(&buf, root, REGISTER_V0, &context);
	/* emit_move(&buf, registers_argument_nr[0], REGISTER_V0); */
	emit_pop(&buf, REGISTER_GP);
	emit_jreturn(&buf);
	buffer_terminate(buf);
	return buf;
}
