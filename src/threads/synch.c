/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
*/

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

void
sema_init (struct semaphore *sema, unsigned value) 
{
  ASSERT (sema != NULL);
  sema->value = value;
  list_init (&sema->waiters);
}

void
sema_down (struct semaphore *sema) 
{
  enum intr_level old_level;
  ASSERT (sema != NULL);
  ASSERT (!intr_context ());
  old_level = intr_disable ();
  while (sema->value == 0) 
    {
      /* Insert waiter in priority order. */
      list_insert_ordered (&sema->waiters, &thread_current ()->elem, thread_cmp_priority, NULL);
      thread_block ();
    }
  sema->value--;
  intr_set_level (old_level);
}

bool
sema_try_down (struct semaphore *sema) 
{
  enum intr_level old_level;
  bool success;
  ASSERT (sema != NULL);
  old_level = intr_disable ();
  if (sema->value > 0) 
    {
      sema->value--;
      success = true; 
    }
  else
    success = false;
  intr_set_level (old_level);
  return success;
}

void
sema_up (struct semaphore *sema) 
{
  enum intr_level old_level;
  ASSERT (sema != NULL);
  old_level = intr_disable ();
  if (!list_empty (&sema->waiters)) 
    {
      /* Sort waiters to ensure highest priority is unblocked first. */
      list_sort (&sema->waiters, thread_cmp_priority, NULL);
      thread_unblock (list_entry (list_pop_front (&sema->waiters), struct thread, elem));
    }
  sema->value++;
  intr_set_level (old_level);

  /* Preempt if a higher-priority thread is now ready. */
  thread_yield_if_not_highest ();
}

void
lock_init (struct lock *lock)
{
  ASSERT (lock != NULL);
  lock->holder = NULL;
  sema_init (&lock->semaphore, 1);
}

void
lock_acquire (struct lock *lock)
{
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (!lock_held_by_current_thread (lock));

  struct thread *cur = thread_current ();
  if (lock->holder != NULL) 
    {
      cur->lock_waiting = lock;
      thread_donate_priority ();
    }

  sema_down (&lock->semaphore);
  
  cur->lock_waiting = NULL;
  lock->holder = cur;
  list_push_back (&cur->locks_held, &lock->elem);
}

bool
lock_try_acquire (struct lock *lock)
{
  bool success;
  ASSERT (lock != NULL);
  ASSERT (!lock_held_by_current_thread (lock));
  success = sema_try_down (&lock->semaphore);
  if (success)
    {
      lock->holder = thread_current ();
      list_push_back (&thread_current ()->locks_held, &lock->elem);
    }
  return success;
}

void
lock_release (struct lock *lock) 
{
  ASSERT (lock != NULL);
  ASSERT (lock_held_by_current_thread (lock));

  struct thread *cur = thread_current ();
  list_remove (&lock->elem);
  lock->holder = NULL;
  thread_update_priority (cur);
  sema_up (&lock->semaphore);
}

bool
lock_held_by_current_thread (const struct lock *lock) 
{
  ASSERT (lock != NULL);
  return lock->holder == thread_current ();
}

/* Condition Variables */

struct semaphore_elem 
  {
    struct list_elem elem;
    struct semaphore semaphore;
  };

void
cond_init (struct condition *cond)
{
  ASSERT (cond != NULL);
  list_init (&cond->waiters);
}

void
cond_wait (struct condition *cond, struct lock *lock) 
{
  struct semaphore_elem waiter;
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));
  
  sema_init (&waiter.semaphore, 0);
  list_push_back (&cond->waiters, &waiter.elem);
  lock_release (lock);
  sema_down (&waiter.semaphore);
  lock_acquire (lock);
}

/* Helper to compare priority of threads waiting on condition semaphores. */
static bool 
cond_sema_priority (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED) 
{
  struct semaphore_elem *sa = list_entry((struct list_elem *) a, struct semaphore_elem, elem);
  struct semaphore_elem *sb = list_entry((struct list_elem *) b, struct semaphore_elem, elem);
  
  struct thread *ta = list_entry(list_begin(&sa->semaphore.waiters), struct thread, elem);
  struct thread *tb = list_entry(list_begin(&sb->semaphore.waiters), struct thread, elem);
  
  return ta->priority > tb->priority;
}

void
cond_signal (struct condition *cond, struct lock *lock UNUSED) 
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));

  if (!list_empty (&cond->waiters)) 
    {
      list_sort (&cond->waiters, cond_sema_priority, NULL);
      sema_up (&list_entry (list_pop_front (&cond->waiters), struct semaphore_elem, elem)->semaphore);
    }
}

void
cond_broadcast (struct condition *cond, struct lock *lock) 
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  while (!list_empty (&cond->waiters))
    cond_signal (cond, lock);
}