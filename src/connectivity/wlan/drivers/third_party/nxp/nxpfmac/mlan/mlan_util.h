/** @file mlan_util.h
 *
 *  @brief This file contains wrappers for linked-list,
 *  spinlock and timer defines.
 *
 *
 *  Copyright 2008-2021 NXP
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright notice,
 *  this list of conditions and the following disclaimer.
 *
 *  2. Redistributions in binary form must reproduce the above copyright notice,
 *  this list of conditions and the following disclaimer in the documentation
 *  and/or other materials provided with the distribution.
 *
 *  3. Neither the name of the copyright holder nor the names of its
 *  contributors may be used to endorse or promote products derived from this
 *  software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ASIS AND
 *  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 */

/******************************************************
Change log:
    10/28/2008: initial version
******************************************************/

#ifndef _MLAN_UTIL_H_
#define _MLAN_UTIL_H_

/** Circular doubly linked list */
typedef struct _mlan_linked_list {
	/** Pointer to previous node */
	struct _mlan_linked_list *pprev;
	/** Pointer to next node */
	struct _mlan_linked_list *pnext;
} mlan_linked_list, *pmlan_linked_list;

/** List head */
typedef struct _mlan_list_head {
	/** Pointer to previous node */
	struct _mlan_linked_list *pprev;
	/** Pointer to next node */
	struct _mlan_linked_list *pnext;
	/** Pointer to lock */
	t_void *plock;
} mlan_list_head, *pmlan_list_head;

/** MLAN MNULL pointer */
#define MNULL	((void *)0)

/**
 *  @brief This function initializes a list without locking
 *
 *  @param phead		List head
 *
 *  @return			N/A
 */
static INLINE t_void
util_init_list(pmlan_linked_list phead)
{
	/* Both next and prev point to self */
	phead->pprev = phead->pnext = (pmlan_linked_list)phead;
}

/**
 *  @brief This function initializes a list
 *
 *  @param phead		List head
 *  @param lock_required	A flag for spinlock requirement
 *  @param moal_init_lock	A pointer to init lock handler
 *
 *  @return			N/A
 */
static INLINE t_void
util_init_list_head(t_void *pmoal_handle, pmlan_list_head phead,
		    t_u8 lock_required,
		    mlan_status (*moal_init_lock) (t_void *handle,
						   t_void **pplock))
{
	/* Both next and prev point to self */
	util_init_list((pmlan_linked_list)phead);
	if (lock_required)
		moal_init_lock(pmoal_handle, &phead->plock);
	else
		phead->plock = MNULL;
}

/**
 *  @brief This function frees a list
 *
 *  @param phead		List head
 *  @param moal_free_lock	A pointer to free lock handler
 *
 *  @return			N/A
 */
static INLINE t_void
util_free_list_head(t_void *pmoal_handle, pmlan_list_head phead,
		    mlan_status (*moal_free_lock) (t_void *handle,
						   t_void *plock))
{
	phead->pprev = phead->pnext = MNULL;
	if (phead->plock)
		moal_free_lock(pmoal_handle, phead->plock);
}

/**
 *  @brief This function peeks into a list
 *
 *  @param phead		List head
 *  @param moal_spin_lock	A pointer to spin lock handler
 *  @param moal_spin_unlock	A pointer to spin unlock handler
 *
 *  @return			List node
 */
static INLINE pmlan_linked_list
util_peek_list(t_void *pmoal_handle, pmlan_list_head phead,
	       mlan_status (*moal_spin_lock) (t_void *handle, t_void *plock),
	       mlan_status (*moal_spin_unlock) (t_void *handle, t_void *plock))
{
	pmlan_linked_list pnode = MNULL;

	if (moal_spin_lock)
		moal_spin_lock(pmoal_handle, phead->plock);
	if (phead->pnext != (pmlan_linked_list)phead)
		pnode = phead->pnext;
	if (moal_spin_unlock)
		moal_spin_unlock(pmoal_handle, phead->plock);
	return pnode;
}

/**
 *  @brief This function queues a node at the list tail
 *
 *  @param phead		List head
 *  @param pnode		List node to queue
 *  @param moal_spin_lock	A pointer to spin lock handler
 *  @param moal_spin_unlock	A pointer to spin unlock handler
 *
 *  @return			N/A
 */
static INLINE t_void
util_enqueue_list_tail(t_void *pmoal_handle, pmlan_list_head phead,
		       pmlan_linked_list pnode,
		       mlan_status (*moal_spin_lock) (t_void *handle,
						      t_void *plock),
		       mlan_status (*moal_spin_unlock) (t_void *handle,
							t_void *plock))
{
	pmlan_linked_list pold_last;

	if (moal_spin_lock)
		moal_spin_lock(pmoal_handle, phead->plock);
	pold_last = phead->pprev;
	pnode->pprev = pold_last;
	pnode->pnext = (pmlan_linked_list)phead;

	phead->pprev = pold_last->pnext = pnode;
	if (moal_spin_unlock)
		moal_spin_unlock(pmoal_handle, phead->plock);
}

/**
 *  @brief This function adds a node at the list head
 *
 *  @param phead		List head
 *  @param pnode		List node to add
 *  @param moal_spin_lock	A pointer to spin lock handler
 *  @param moal_spin_unlock	A pointer to spin unlock handler
 *
 *  @return			N/A
 */
static INLINE t_void
util_enqueue_list_head(t_void *pmoal_handle, pmlan_list_head phead,
		       pmlan_linked_list pnode,
		       mlan_status (*moal_spin_lock) (t_void *handle,
						      t_void *plock),
		       mlan_status (*moal_spin_unlock) (t_void *handle,
							t_void *plock))
{
	pmlan_linked_list pold_first;

	if (moal_spin_lock)
		moal_spin_lock(pmoal_handle, phead->plock);
	pold_first = phead->pnext;
	pnode->pprev = (pmlan_linked_list)phead;
	pnode->pnext = pold_first;

	phead->pnext = pold_first->pprev = pnode;
	if (moal_spin_unlock)
		moal_spin_unlock(pmoal_handle, phead->plock);
}

/**
 *  @brief This function removes a node from the list
 *
 *  @param phead		List head
 *  @param pnode		List node to remove
 *  @param moal_spin_lock	A pointer to spin lock handler
 *  @param moal_spin_unlock	A pointer to spin unlock handler
 *
 *  @return			N/A
 */
static INLINE t_void
util_unlink_list(t_void *pmoal_handle, pmlan_list_head phead,
		 pmlan_linked_list pnode,
		 mlan_status (*moal_spin_lock) (t_void *handle, t_void *plock),
		 mlan_status (*moal_spin_unlock) (t_void *handle,
						  t_void *plock))
{
	pmlan_linked_list pmy_prev;
	pmlan_linked_list pmy_next;

	if (moal_spin_lock)
		moal_spin_lock(pmoal_handle, phead->plock);
	pmy_prev = pnode->pprev;
	pmy_next = pnode->pnext;
	pmy_next->pprev = pmy_prev;
	pmy_prev->pnext = pmy_next;

	pnode->pnext = pnode->pprev = MNULL;
	if (moal_spin_unlock)
		moal_spin_unlock(pmoal_handle, phead->plock);
}

/**
 *  @brief This function dequeues a node from the list
 *
 *  @param phead		List head
 *  @param moal_spin_lock	A pointer to spin lock handler
 *  @param moal_spin_unlock	A pointer to spin unlock handler
 *
 *  @return			List node
 */
static INLINE pmlan_linked_list
util_dequeue_list(t_void *pmoal_handle, pmlan_list_head phead,
		  mlan_status (*moal_spin_lock) (t_void *handle, t_void *plock),
		  mlan_status (*moal_spin_unlock) (t_void *handle,
						   t_void *plock))
{
	pmlan_linked_list pnode;

	if (moal_spin_lock)
		moal_spin_lock(pmoal_handle, phead->plock);
	pnode = phead->pnext;
	if (pnode && (pnode != (pmlan_linked_list)phead))
		util_unlink_list(pmoal_handle, phead, pnode, MNULL, MNULL);
	else
		pnode = MNULL;
	if (moal_spin_unlock)
		moal_spin_unlock(pmoal_handle, phead->plock);
	return pnode;
}

/** Access controlled scalar variable */
typedef struct _mlan_scalar {
	/** Value */
	t_s32 value;
	/** Pointer to lock */
	t_void *plock;
	/** Control flags */
	t_u32 flags;
} mlan_scalar, *pmlan_scalar;

/** Flag to scalar lock acquired */
#define MLAN_SCALAR_FLAG_UNIQUE_LOCK MBIT(16)

/** scalar conditional value list */
typedef enum _MLAN_SCALAR_CONDITIONAL {
	MLAN_SCALAR_COND_EQUAL,
	MLAN_SCALAR_COND_NOT_EQUAL,
	MLAN_SCALAR_COND_GREATER_THAN,
	MLAN_SCALAR_COND_GREATER_OR_EQUAL,
	MLAN_SCALAR_COND_LESS_THAN,
	MLAN_SCALAR_COND_LESS_OR_EQUAL
} MLAN_SCALAR_CONDITIONAL;

/**
 *  @brief This function initializes a scalar
 *
 *  @param pscalar			Pointer to scalar
 *  @param val				Initial scalar value
 *  @param plock_to_use		A new lock is created if NULL, else lock to use
 *  @param moal_init_lock	A pointer to init lock handler
 *
 *  @return					N/A
 */
static INLINE t_void
util_scalar_init(t_void *pmoal_handle, pmlan_scalar pscalar, t_s32 val,
		 t_void *plock_to_use,
		 mlan_status (*moal_init_lock) (t_void *handle,
						t_void **pplock))
{
	pscalar->value = val;
	pscalar->flags = 0;
	if (plock_to_use) {
		pscalar->flags &= ~MLAN_SCALAR_FLAG_UNIQUE_LOCK;
		pscalar->plock = plock_to_use;
	} else {
		pscalar->flags |= MLAN_SCALAR_FLAG_UNIQUE_LOCK;
		moal_init_lock(pmoal_handle, &pscalar->plock);
	}
}

/**
 *  @brief This function frees a scalar
 *
 *  @param pscalar			Pointer to scalar
 *  @param moal_free_lock	A pointer to free lock handler
 *
 *  @return			N/A
 */
static INLINE t_void
util_scalar_free(t_void *pmoal_handle, pmlan_scalar pscalar,
		 mlan_status (*moal_free_lock) (t_void *handle, t_void *plock))
{
	if (pscalar->flags & MLAN_SCALAR_FLAG_UNIQUE_LOCK)
		moal_free_lock(pmoal_handle, pscalar->plock);
}

/**
 *  @brief This function reads value from scalar
 *
 *  @param pscalar			Pointer to scalar
 *  @param moal_spin_lock	A pointer to spin lock handler
 *  @param moal_spin_unlock	A pointer to spin unlock handler
 *
 *  @return					Stored value
 */
static INLINE t_s32
util_scalar_read(t_void *pmoal_handle, pmlan_scalar pscalar,
		 mlan_status (*moal_spin_lock) (t_void *handle, t_void *plock),
		 mlan_status (*moal_spin_unlock) (t_void *handle,
						  t_void *plock))
{
	t_s32 val;

	if (moal_spin_lock)
		moal_spin_lock(pmoal_handle, pscalar->plock);
	val = pscalar->value;
	if (moal_spin_unlock)
		moal_spin_unlock(pmoal_handle, pscalar->plock);

	return val;
}

/**
 *  @brief This function writes value to scalar
 *
 *  @param pscalar			Pointer to scalar
 *  @param val				Value to write
 *  @param moal_spin_lock	A pointer to spin lock handler
 *  @param moal_spin_unlock	A pointer to spin unlock handler
 *
 *  @return					N/A
 */
static INLINE t_void
util_scalar_write(t_void *pmoal_handle, pmlan_scalar pscalar, t_s32 val,
		  mlan_status (*moal_spin_lock) (t_void *handle, t_void *plock),
		  mlan_status (*moal_spin_unlock) (t_void *handle,
						   t_void *plock))
{
	if (moal_spin_lock)
		moal_spin_lock(pmoal_handle, pscalar->plock);
	pscalar->value = val;
	if (moal_spin_unlock)
		moal_spin_unlock(pmoal_handle, pscalar->plock);
}

/**
 *  @brief This function increments the value in scalar
 *
 *  @param pscalar			Pointer to scalar
 *  @param moal_spin_lock	A pointer to spin lock handler
 *  @param moal_spin_unlock	A pointer to spin unlock handler
 *
 *  @return					N/A
 */
static INLINE t_void
util_scalar_increment(t_void *pmoal_handle, pmlan_scalar pscalar,
		      mlan_status (*moal_spin_lock) (t_void *handle,
						     t_void *plock),
		      mlan_status (*moal_spin_unlock) (t_void *handle,
						       t_void *plock))
{
	if (moal_spin_lock)
		moal_spin_lock(pmoal_handle, pscalar->plock);
	pscalar->value++;
	if (moal_spin_unlock)
		moal_spin_unlock(pmoal_handle, pscalar->plock);
}

/**
 *  @brief This function decrements the value in scalar
 *
 *  @param pscalar			Pointer to scalar
 *  @param moal_spin_lock	A pointer to spin lock handler
 *  @param moal_spin_unlock	A pointer to spin unlock handler
 *
 *  @return					N/A
 */
static INLINE t_void
util_scalar_decrement(t_void *pmoal_handle, pmlan_scalar pscalar,
		      mlan_status (*moal_spin_lock) (t_void *handle,
						     t_void *plock),
		      mlan_status (*moal_spin_unlock) (t_void *handle,
						       t_void *plock))
{
	if (moal_spin_lock)
		moal_spin_lock(pmoal_handle, pscalar->plock);
	pscalar->value--;
	if (moal_spin_unlock)
		moal_spin_unlock(pmoal_handle, pscalar->plock);
}

/**
 *  @brief This function adds an offset to the value in scalar,
 *         and returns the new value
 *
 *  @param pscalar			Pointer to scalar
 *  @param offset			Offset value (can be negative)
 *  @param moal_spin_lock	A pointer to spin lock handler
 *  @param moal_spin_unlock	A pointer to spin unlock handler
 *
 *  @return					Value after offset
 */
static INLINE t_s32
util_scalar_offset(t_void *pmoal_handle, pmlan_scalar pscalar, t_s32 offset,
		   mlan_status (*moal_spin_lock) (t_void *handle,
						  t_void *plock),
		   mlan_status (*moal_spin_unlock) (t_void *handle,
						    t_void *plock))
{
	t_s32 newval;

	if (moal_spin_lock)
		moal_spin_lock(pmoal_handle, pscalar->plock);
	newval = (pscalar->value += offset);
	if (moal_spin_unlock)
		moal_spin_unlock(pmoal_handle, pscalar->plock);

	return newval;
}

/**
 *  @brief This function writes the value to the scalar
 *         if existing value compared with other value is true.
 *
 *  @param pscalar          Pointer to scalar
 *  @param condition        Condition to check
 *  @param val_compare      Value to compare against current value
 *                          ((A X B), where B = val_compare)
 *  @param val_to_set       Value to set if comparison is true
 *  @param moal_spin_lock   A pointer to spin lock handler
 *  @param moal_spin_unlock A pointer to spin unlock handler
 *
 *  @return                 Comparison result (MTRUE or MFALSE)
 */
static INLINE t_u8
util_scalar_conditional_write(t_void *pmoal_handle, pmlan_scalar pscalar,
			      MLAN_SCALAR_CONDITIONAL condition,
			      t_s32 val_compare, t_s32 val_to_set,
			      mlan_status (*moal_spin_lock) (t_void *handle,
							     t_void *plock),
			      mlan_status (*moal_spin_unlock) (t_void *handle,
							       t_void *plock))
{
	t_u8 update;
	if (moal_spin_lock)
		moal_spin_lock(pmoal_handle, pscalar->plock);

	switch (condition) {
	case MLAN_SCALAR_COND_EQUAL:
		update = (pscalar->value == val_compare);
		break;
	case MLAN_SCALAR_COND_NOT_EQUAL:
		update = (pscalar->value != val_compare);
		break;
	case MLAN_SCALAR_COND_GREATER_THAN:
		update = (pscalar->value > val_compare);
		break;
	case MLAN_SCALAR_COND_GREATER_OR_EQUAL:
		update = (pscalar->value >= val_compare);
		break;
	case MLAN_SCALAR_COND_LESS_THAN:
		update = (pscalar->value < val_compare);
		break;
	case MLAN_SCALAR_COND_LESS_OR_EQUAL:
		update = (pscalar->value <= val_compare);
		break;
	default:
		update = MFALSE;
		break;
	}
	if (update)
		pscalar->value = val_to_set;

	if (moal_spin_unlock)
		moal_spin_unlock(pmoal_handle, pscalar->plock);
	return (update) ? MTRUE : MFALSE;
}

/**
 *  @brief This function counts the bits of unsigned int number
 *
 *  @param num  number
 *  @return     number of bits
 */
static INLINE t_u32
bitcount(t_u32 num)
{
	t_u32 count = 0;
	static t_u32 nibblebits[] = { 0, 1, 1, 2, 1, 2, 2, 3,
		1, 2, 2, 3, 2, 3, 3, 4
	};
	for (; num != 0; num >>= 4)
		count += nibblebits[num & 0x0f];
	return count;
}

#endif /* !_MLAN_UTIL_H_ */
