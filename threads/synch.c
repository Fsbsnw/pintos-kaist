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

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
   decrement it.

   - up or "V": increment the value (and wake up one waiting
   thread, if any). */

/* 정해진 value 값으로 먼저 semaphore의 counting value를 초기화 해준다. */
void
sema_init (struct semaphore *sema, unsigned value) {
	ASSERT (sema != NULL);

	sema->value = value;
	list_init (&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. This is
   sema_down function. */
void
sema_down (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);
	ASSERT (!intr_context ());

	old_level = intr_disable ();

	/* value의 값이 0이면 해당 공유 자원을 사용할 수 없다. */
	/* 해당 스레드가 while문을 돌면서 계속 value의 값이 up 되기를 기다리고 있다. */
	/* 스레드가 block되었으므로 여기서 코드가 멈춘다. */
	while (sema->value == 0) {
		list_insert_ordered (&sema->waiters, &thread_current ()->elem, cmp_priority, 0);
		thread_block ();
	}

	/* UP이 되어 while문을 빠져나온 다음 공유 자원을 차지했다. */
	/* 자신이 공유자원을 사용중이므로 value를 DOWN한다. */
	sema->value--;
	intr_set_level (old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool
sema_try_down (struct semaphore *sema) {
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

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */

/* waiting list에서 그 다음 스레드가 공유자원을 사용할 수 있도록 unblock */
void
sema_up (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (!list_empty (&sema->waiters)){
	list_sort(&sema->waiters, cmp_priority, 0);  // Nested donation으로 인해 변경된 우선순위

	thread_unblock (list_entry (list_pop_front (&sema->waiters), struct thread, elem));
	}
	sema->value++;
	test_max_priority();
	intr_set_level (old_level);
}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void
sema_self_test (void) {
	struct semaphore sema[2];
	int i;

	printf ("Testing semaphores...");
	sema_init (&sema[0], 0);
	sema_init (&sema[1], 0);
	thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up (&sema[0]);
		sema_down (&sema[1]);
	}
	printf ("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper (void *sema_) {
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down (&sema[0]);
		sema_up (&sema[1]);
	}
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void
lock_init (struct lock *lock) {
	ASSERT (lock != NULL);

	lock->holder = NULL;
	sema_init (&lock->semaphore, 1); // value를 1로 초기화
}


bool thread_compare_donate_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED){
	struct thread* thread_a = list_entry(a, struct thread, donation_elem);
	struct thread* thread_b = list_entry(b, struct thread, donation_elem);

	return thread_a->priority > thread_b->priority;
}

void donate_priority(){
	int depth;
	struct thread* curr = thread_current();

	/* 최대 depth는 8이다. */
	for (depth = 0; depth < 8; depth++){
		if (!curr->wait_on_lock)   // 더 이상 nested가 없을 때.
			break;
		
		struct thread* holder = curr->wait_on_lock->holder;
		holder->priority = curr->priority;   // 우선 순위를 donation한다.
		curr = holder;  //  그 다음 depth로 들어간다.
	}
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
lock_acquire (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (!lock_held_by_current_thread (lock));

	struct thread* curr = thread_current();


	/* 만약 해당 lock을 누가 사용하고 있다면 */
	if (lock->holder != NULL){
		curr->wait_on_lock = lock;  // 현재 스레드의 wait_on_lock에 해당 lock을 저장한다.
		// 지금 lock을 소유하고 있는 스레드의 donations에 현재 스레드를 저장한다.
		list_insert_ordered(&lock->holder->donations, &curr->donation_elem, 
		thread_compare_donate_priority, 0);

		donate_priority();
	}

	sema_down (&lock->semaphore);

	curr->wait_on_lock = NULL;  // lock을 획득했으므로 대기하고 있는 lock이 이제는 없다.

	lock->holder = thread_current ();
}



/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool
lock_try_acquire (struct lock *lock) {
	bool success;

	ASSERT (lock != NULL);
	ASSERT (!lock_held_by_current_thread (lock));

	success = sema_try_down (&lock->semaphore);
	if (success)
		lock->holder = thread_current ();
	return success;
}


void remove_with_lock(struct lock* lock){
	struct list_elem* e;
	struct thread* curr = thread_current();

	for (e = list_begin(&curr->donations); e != list_end(&curr->donations); e = list_next(e)){
		struct thread* t = list_entry(e, struct thread, donation_elem);
		if (t->wait_on_lock == lock){
			list_remove(&t->donation_elem);
		}
	}
}


void refresh_priority(){
	struct thread* curr = thread_current();

	curr -> priority = curr->init_priority;  // 우선 원복해준다.

	/* donation을 받고 있다면 */
	if (!list_empty(&curr->donations)){
			list_sort(&curr->donations, thread_compare_donate_priority, 0);
	
			struct thread* front = list_entry(list_front(&curr->donations), struct thread, donation_elem);
			
			if(front->priority > curr->priority)  // 만약 초기 우선 순위보다 더 큰 값이라면
				curr->priority = front->priority;
	}
}
/* Releases LOCK, which must be owned by the current thread.
   This is lock_release function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void
lock_release (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (lock_held_by_current_thread (lock));

	remove_with_lock(lock); // donations 리스트에서 해당 lock을 필요로 하는 스레드를 없애준다.
	refresh_priority();  // 현재 스레드의 priority를 업데이트한다.

	lock->holder = NULL;
	sema_up (&lock->semaphore); // sema를 up시켜 해당 Lock에서 기다리고 있는 스레드를 하나 깨운다.
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock) {
	ASSERT (lock != NULL);

	return lock->holder == thread_current ();
}


/* One semaphore in a list. */
struct semaphore_elem {
	struct list_elem elem;              /* List element. */
	struct semaphore semaphore;         /* This semaphore. */
};

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond) {
	ASSERT (cond != NULL);

	list_init (&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
cond_wait (struct condition *cond, struct lock *lock) {
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

/* thread의 우선순위로 비교해서 정렬  */
bool sema_compare_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED){
	struct semaphore_elem* sema_a = list_entry(a, struct semaphore_elem, elem);
	struct semaphore_elem* sema_b = list_entry(b, struct semaphore_elem, elem);

	struct list_elem* waiting_list_a = &(sema_a->semaphore.waiters);
	struct list_elem* waiting_list_b = &(sema_b->semaphore.waiters);

	struct thread* thread_a = list_entry(list_front(waiting_list_a), struct thread, elem);
	struct thread* thread_b = list_entry(list_front(waiting_list_b), struct thread, elem);

	return thread_a->priority > thread_b->priority;
}
/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
   
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	if (!list_empty (&cond->waiters)){
		list_sort(&cond->waiters, sema_compare_priority, 0);   // 정렬.
		sema_up (&list_entry (list_pop_front (&cond->waiters),
					struct semaphore_elem, elem)->semaphore);
	}
}


/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_broadcast (struct condition *cond, struct lock *lock) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);

	while (!list_empty (&cond->waiters))
		cond_signal (cond, lock);
}
