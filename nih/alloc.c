/* libnih
 *
 * alloc.c - hierarchial allocator
 *
 * Copyright © 2006 Scott James Remnant <scott@netsplit.com>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */


#include <stdlib.h>

#include <nih/macros.h>
#include <nih/logging.h>
#include <nih/list.h>

#include "alloc.h"


/**
 * NihAllocCtx:
 * @entry: list header,
 * @parent: parent context, when freed we will be,
 * @children: child blocks that will be freed when we are,
 * @size: size of requested allocation,
 * @allocator: function to call to return memory,
 * @destructor: function to be called when freed.
 *
 * This structure is placed before all allocations in memory and is used
 * to build up a tree of them.  When an allocation is freed, all children
 * are also freed and any destructors are called.
 **/
typedef struct nih_alloc_ctx {
	NihList               entry;

	struct nih_alloc_ctx *parent;
	NihList               children;

	size_t                size;

	NihAllocator          allocator;
	NihDestructor         destructor;
} NihAllocCtx;

/**
 * NIH_ALLOC_CTX:
 * @ptr: pointer to block of memory.
 *
 * Obtain the location of the #NihAllocCtx structure given a pointer to the
 * block of memory beyond it.
 *
 * Returns: pointer to #NihAllocCtx structure.
 **/
#define NIH_ALLOC_CTX(ptr) ((NihAllocCtx *)(ptr) - 1)

/**
 * NIH_ALLOC_PTR:
 * @ctx: pointer to #NihAllocCtx structure.
 *
 * Obtain the location of the block of memory given a pointer to the
 * #NihAllocCtx structure in front of it.
 *
 * Returns: pointer to block of memory.
 **/
#define NIH_ALLOC_PTR(ctx) ((void *)((NihAllocCtx *)(ctx) + 1))


/**
 * allocator:
 *
 * Function used to allocate and free memory for the majority of blocks.
 **/
static NihAllocator allocator = NULL;


/**
 * nih_alloc_init:
 *
 * Initialise the default allocator.
 **/
static void
nih_alloc_init (void)
{
	if (! allocator)
		nih_alloc_set_allocator (realloc);
}

/**
 * nih_alloc_set_allocator:
 * @new_allocator: new default allocator function.
 *
 * Sets the function that will be used to allocate memory for all further
 * blocks requested and return it to the system.  The behaviour of the
 * function should be the same of that as the standard #realloc function.
 *
 * This function should generally only be used in the initialisation
 * portion of your program, and should not be used to switch allocators
 * temporarily.  Use #nih_alloc_using to allocate a block with an
 * alternate allocator.
 **/
void
nih_alloc_set_allocator (NihAllocator new_allocator)
{
	nih_assert (new_allocator != NULL);

	allocator = new_allocator;
}


/**
 * nih_alloc_using:
 * @allocator: allocator to use for this block,
 * @parent: parent block of allocation,
 * @size: size of requested block.
 *
 * Allocates a block of memory of at least @size bytes with the @allocator
 * function and returns a pointer to it.
 *
 * If @parent is not %NULL, it should be a pointer to another allocated
 * block which will be used as the parent for this block.  When @parent
 * is freed, the returned block will be freed too.  If you have clean-up
 * that would need to be run, you can assign a destructor function using
 * the #nih_alloc_set_destructor function.
 *
 * Returns: requested memory block or %NULL if allocation fails.
 **/
void *
nih_alloc_using (NihAllocator  allocator,
		 void         *parent,
		 size_t        size)
{
	NihAllocCtx *ctx;

	nih_assert (allocator != NULL);

	ctx = allocator (NULL, sizeof (NihAllocCtx) + size);
	if (! ctx)
		return NULL;

	nih_list_init (&ctx->entry);
	nih_list_init (&ctx->children);

	ctx->size = size;

	ctx->allocator = allocator;
	ctx->destructor = NULL;

	if (parent) {
		ctx->parent = NIH_ALLOC_CTX (parent);

		nih_list_add (&ctx->parent->children, &ctx->entry);
	} else {
		ctx->parent = NULL;
	}

	return NIH_ALLOC_PTR (ctx);
}

/**
 * nih_alloc:
 * @parent: parent block of allocation,
 * @size: size of requested block.
 *
 * Allocates a block of memory of at least @size bytes and returns
 * a pointer to it.
 *
 * If @parent is not %NULL, it should be a pointer to another allocated
 * block which will be used as the parent for this block.  When @parent
 * is freed, the returned block will be freed too.  If you have clean-up
 * that would need to be run, you can assign a destructor function using
 * the #nih_alloc_set_destructor function.
 *
 * Returns: requested memory block or %NULL if allocation fails.
 **/
void *
nih_alloc (void   *parent,
	   size_t  size)
{
	if (! allocator)
		nih_alloc_init ();

	return nih_alloc_using (allocator, parent, size);
}


/**
 * nih_free:
 * @ptr: pointer to block to free.
 *
 * Return the block of memory at @ptr to the allocator so it may be
 * re-used by something else.  Any children of the block are also freed,
 * and any destructors called.
 *
 * Returns: return value from destructor, or 0.
 **/
int
nih_free (void *ptr)
{
	NihAllocCtx *ctx;
	NihList     *iter;
	int          ret = 0;

	nih_assert (ptr != NULL);

	ctx = NIH_ALLOC_CTX (ptr);

	iter = ctx->children.next;
	while (iter != &ctx->children) {
		void *ptr;

		ptr = NIH_ALLOC_PTR (iter);
		iter = iter->next;

		ret = nih_free (ptr);
	}

	if (ctx->destructor)
		ret = ctx->destructor (ptr);

	ctx->allocator (ctx, 0);

	return ret;
}

/**
 * nih_alloc_set_destructor:
 * @ptr: pointer to block,
 * @destructor: destructor function to set.
 *
 * Sets the destructor function of the block to @destructor, which may be
 * %NULL.
 *
 * The destructor function will be called when the block is freed, either
 * directly or as a result as a parent being freed.  The block will be
 * passed as a pointer to the destructor, and the destructor may return
 * a value which will be the return value of the #nih_free function.
 **/
void
nih_alloc_set_destructor (void          *ptr,
			  NihDestructor  destructor)
{
	NihAllocCtx *ctx;

	nih_assert (ptr != NULL);

	ctx = NIH_ALLOC_CTX (ptr);
	ctx->destructor = destructor;
}


/**
 * nih_alloc_size:
 * @ptr: pointer to block.
 *
 * Returns: the size of the allocated block, excluding the context.
 **/
size_t
nih_alloc_size (void *ptr)
{
	NihAllocCtx *ctx;

	nih_assert (ptr != NULL);

	ctx = NIH_ALLOC_CTX (ptr);
	return ctx->size;
}

/**
 * nih_alloc_parent:
 * @ptr: pointer to block.
 *
 * Returns: the parent block or %NULL if none.
 **/
void *
nih_alloc_parent (void *ptr)
{
	NihAllocCtx *ctx;

	nih_assert (ptr != NULL);

	ctx = NIH_ALLOC_CTX (ptr);
	if (ctx->parent) {
		return NIH_ALLOC_PTR (ctx->parent);
	} else {
		return NULL;
	}
}
