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

/*e fixed-size bit vectors */

#ifndef _ATTOL_BITVECTOR_H
#define _ATTOL_BITVECTOR_H

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>

/*e
 * Bitvectors are implemented in one of two shapes:  either as a `small' bitvector,
 * stored in a single register (up to BITVECTOR_SMALL_MAX bits) or as a `large'
 * bitvector (unbounded size), allocated on the heap.  The underlying assumption is that
 * small bitvectors are very common in practice.
 */
/*d
 * Bitvektoren sind auf zwei Weisen implementiert:  entweder als `kleine' (SMALL) Bitvektoren,
 * in einem einzelnen Register gesichert (mit bis zu BITVECTOR_SMALL_MAX bits), oder als
 * `grosse' Bitvektoren (ohne Groessenbeschraenkung), die im C-Ablagespeicher liegen.
 * Dies ist eine Repräsentierungsoptimierung: wir erwarten, dass kleine Bitvektoren sehr
 * haeufig auftreten.
 */

#define BITVECTOR_INLINE

union bitvector {
	unsigned long long small;
	unsigned long long *large;
};
typedef union bitvector bitvector_t;

#define BITVECTOR_TO_POINTER(BV) ((void *)((BV).large))
#define BITVECTOR_FROM_POINTER(P) ((bitvector_t)((unsigned long long *)(P)))

#define BITVECTOR_SIZE_SHIFT 1
#define BITVECTOR_BODY_SHIFT 7
#define BITVECTOR_SMALL_MAX (64 - BITVECTOR_BODY_SHIFT)
#define BITVECTOR_SIZE_MASK 63ull

#define BITVECTOR_IS_SMALL(BV) (((BV).small & 1))

#define BITVECTOR_MAKE_SMALL(size, v) {.small = ((((unsigned long long) v) << BITVECTOR_BODY_SHIFT) | (((unsigned long long) size) << BITVECTOR_SIZE_SHIFT) | 1) }

/*d
 * Die Bitvektor-Zugriffsschnittstelle ist über Makros definiert:
 *
 * - BITVECTOR_SET(bv, b)	setzt Bit #b im Bitvektor bv.  Rückgabewert ist `bv' nach der Aktualisierung.
 *                              Das vorherige `bv' ist nun ungueltig und muss mit dem Rueckgabewert ueberschrieben werden.
 * - BITVECTOR_CLEAR(bv, b)	löscht Bit #b im Bitvektor bv.  Rückgabe wie bei BITVECTOR_SET.
 * - BITVECTOR_IS_SET(bv, b)	prüft, ob Bit #b im Bitvektor bv gesetzt ist.  Liefert einen Wahrheitswert (0 oder nicht-0)
 * - BITVECTOR_FREE(bv, b)	dealloziert Bitvektor bv.
 */
	
#ifdef BITVECTOR_INLINE
#  define BITVECTOR_SET(BV, B) ((BITVECTOR_IS_SMALL(BV))? (bitvector_t)((BV).small | (1ull << (BITVECTOR_BODY_SHIFT + (B)))) : bitvector_set_slow((BV), (B)))
#  define BITVECTOR_CLEAR(BV, B) ((BITVECTOR_IS_SMALL(BV))? (bitvector_t)((BV).small & ~(1ull << (BITVECTOR_BODY_SHIFT + (B)))) : bitvector_clear_slow((BV), (B)))
#  define BITVECTOR_IS_SET(BV, B) ((BITVECTOR_IS_SMALL(BV))? (((BV).small >> (BITVECTOR_BODY_SHIFT + (B)) & 1ul)) : bitvector_is_set_slow((BV), (B)))
#  define BITVECTOR_FREE(BV) if (!(BITVECTOR_IS_SMALL(BV))) bitvector_free(BV)
#else
#  define BITVECTOR_SET(BV, B) bitvector_set_slow((BV), (B))
#  define BITVECTOR_CLEAR(BV, B) bitvector_clear_slow((BV), (B))
#  define BITVECTOR_IS_SET(BV, B) bitvector_is_set_slow((BV), (B))
#  define BITVECTOR_FREE(BV) bitvector_free(BV)
#endif

/*e
 * Allocates a fresh bit vector with the specified number of bits.
 *
 * All bits are initially cleared.
 */
/*d
 * Alloziert einen neuen Bitvektor mit der angegebenen Größe
 *
 * Alle Bits sind initial nicht gesetzt.
 */
bitvector_t
bitvector_alloc(size_t size);

/*e
 * Allocates a fresh bit vector with the specified number of bits.
 *
 * All bits are initially set.
 */
/*d
 * Alloziert einen neuen Bitvektor mit der angegebenen Größe
 *
 * Alle Bits sind initial gesetzt.
 */
bitvector_t
bitvector_alloc_on(size_t size);

/*e
 * Creates a bitvector from a pre-existing size and unsigned long long value.
 *
 * In the resulting bitvector, bit 0 is set iff the LSB in v is set and so on.
 */
/*d
 * Alloziert einen neuen Bitvektor mit der angegebenen Größe für einen gegebenen long long-Wert.
 *
 * Im zurückgegebenen Bitvektor ist Bit 0 gesetzt gdw das Bit mit geringstem Wert (LSB) in v
 * gesetzt ist usw.
 */
bitvector_t
bitvector(size_t size, unsigned long long v);

/*e
 * Determines the number of bits in the bitvector.
 */
/*d
 * Liefert die Anzahl der Bits im Bitvektor.
 */
size_t
bitvector_size(bitvector_t bitvector);

/*e
 * Deallocates a bitvector.
 */
/*d
 * Dealloziert einen Bitvektor.
 */
void
bitvector_free(bitvector_t bitvector);

/*e
 * Clones a bitvector.
 */
/*d
 * Klont einen Bitvektor (alloziert zusätzlichen Speicher, sowiet nötig).
 */
bitvector_t
bitvector_clone(bitvector_t bitvector);

/*e
 * Creates a new bitvector with the bit-wise disjunction (OR) of the inputs
 *
 * @param bv1, bv2 The two bitvectors to combine
 * @return a bitvector with the disjunction of the bits in bv1 and bv2
 */
bitvector_t
bitvector_or(bitvector_t bv1, bitvector_t bv2);

/*e
 * Creates a new bitvector with the bit-wise conjunction (AND) of the inputs
 *
 * @param bv1, bv2 The two bitvectors to combine
 * @return a bitvector with the conjunction of the bits in bv1 and bv2
 */
bitvector_t
bitvector_and(bitvector_t bv1, bitvector_t bv2);

/*e
 * Determines if any bit set in bv1 is also set in bv2
 *
 * @param bv1, bv2 The two bitvectors to compare
 * @return true iff all bits set in bv1 are also set in bv2
 */
bool
bitvector_is_subset_eq(bitvector_t bv1, bitvector_t bv2);

/*e
 * Sets a bit in a bitvector.  Returns the updated bitvector.
 *
 * Usage:
 *   bv = bitvector_set_slow(bv, pos);
 *
 * With any other usage, results are undefined.
 *
 * Use BITVECTOR_SET(bitvector, pos) for faster access.
 */
bitvector_t
bitvector_set_slow(bitvector_t bitvector, size_t pos);

/*e
 * Clears a bit in a bitvector.  Returns the updated bitvector.
 *
 * Usage:
 *   bv = bitvector_clear_slow(bv, pos);
 *
 * With any other usage, results are undefined.
 *
 * Use BITVECTOR_CLEAR(bitvector, pos) for faster access.
 */
bitvector_t
bitvector_clear_slow(bitvector_t bitvector, size_t pos);

/*e
 * Checks whether a bit in a bitvector is set.
 *
 * Usage:
 *   if (bitvector_is_set(bv, pos)) ...
 *
 * Use BITVECTOR_IS_SET(bitvector, pos) for faster access.
 */
bool
bitvector_is_set_slow(bitvector_t bitvector, size_t pos);

/*e
 * Prints out the bit vector, LSB first
 *
 * @param f File to print to
 * @param bitvector The vector to print
 */
/*d
 * Druckt einen Bitvektor aus, mit dem nullten Bit zuerst.
 *
 * @param f Die Datei für die Ausgabe (z.B. `stdout', um auf die Konsole zu schreiben)
 * @param bitvector The vector to print
 */
void
bitvector_print(FILE *f, bitvector_t bitvector);

#endif // !defined(_ATTOL_BITVECTOR_H)
