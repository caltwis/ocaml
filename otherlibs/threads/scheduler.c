/***********************************************************************/
/*                                                                     */
/*                                OCaml                                */
/*                                                                     */
/*            Xavier Leroy, projet Cristal, INRIA Rocquencourt         */
/*                                                                     */
/*  Copyright 1996 Institut National de Recherche en Informatique et   */
/*  en Automatique.  All rights reserved.  This file is distributed    */
/*  under the terms of the GNU Library General Public License, with    */
/*  the special exception on linking described in file ../../LICENSE.  */
/*                                                                     */
/***********************************************************************/

/* $Id$ */

/* The thread scheduler */

#include <assert.h> // !!!!!!!!!!!!!!!!!
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define CAML_CONTEXT_ROOTS
#define CAML_CONTEXT_STACKS
#define CAML_CONTEXT_BACKTRACE
#define CAML_CONTEXT_CALLBACK
#define CAML_CONTEXT_SIGNALS_BYT
#define CAML_CONTEXT_VMTHREADS
#include "context.h"


#include "alloc.h"
#include "backtrace.h"
#include "callback.h"
#include "config.h"
#include "fail.h"
#include "io.h"
#include "memory.h"
#include "misc.h"
#include "mlvalues.h"
#include "printexc.h"
#include "roots.h"
#include "signals.h"
#include "stacks.h"
#include "sys.h"

#define curr_thread curr_vmthread

#if ! (defined(HAS_SELECT) && \
       defined(HAS_SETITIMER) && \
       defined(HAS_GETTIMEOFDAY) && \
       (defined(HAS_WAITPID) || defined(HAS_WAIT4)))
#include "Cannot compile libthreads, system calls missing"
#endif

#include <errno.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#ifdef HAS_UNISTD
#include <unistd.h>
#endif
#ifdef HAS_SYS_SELECT_H
#include <sys/select.h>
#endif

#ifndef HAS_WAITPID
#define waitpid(pid,status,opts) wait4(pid,status,opts,NULL)
#endif

#ifndef O_NONBLOCK
#define O_NONBLOCK O_NDELAY
#endif

/* Configuration */

/* Initial size of stack when a thread is created (4kB) */
#define Thread_stack_size (Stack_size / 4)

/* Max computation time before rescheduling, in microseconds (50ms) */
#define Thread_timeout 50000

/* The thread descriptors */

struct caml_thread_struct {
  value ident;                  /* Unique id (for equality comparisons) */
  struct caml_thread_struct * next;  /* Double linking of threads */
  struct caml_thread_struct * prev;
  value * stack_low;            /* The execution stack for this thread */
  value * stack_high;
  value * stack_threshold;
  value * sp;
  value * trapsp;
  value backtrace_pos;          /* The backtrace info for this thread */
  code_t * backtrace_buffer;
  value backtrace_last_exn;
  value status;                 /* RUNNABLE, KILLED. etc (see below) */
  value fd;     /* File descriptor on which we're doing read or write */
  value readfds, writefds, exceptfds;
                /* Lists of file descriptors on which we're doing select() */
  value delay;                  /* Time until which this thread is blocked */
  value joining;                /* Thread we're trying to join */
  value waitpid;                /* PID of process we're waiting for */
  value retval;                 /* Value to return when thread resumes */
  struct channel *last_locked_channel; /* The channel to unlock in case an exception is raised */ // !!!!!!!!!!!!!!!
};

typedef struct caml_thread_struct * caml_thread_t;

#define RUNNABLE Val_int(0)
#define KILLED Val_int(1)
#define SUSPENDED Val_int(2)
#define BLOCKED_READ Val_int(4)
#define BLOCKED_WRITE Val_int(8)
#define BLOCKED_SELECT Val_int(16)
#define BLOCKED_DELAY Val_int(32)
#define BLOCKED_JOIN Val_int(64)
#define BLOCKED_WAIT Val_int(128)

#define RESUMED_WAKEUP Val_int(0)
#define RESUMED_DELAY Val_int(1)
#define RESUMED_JOIN Val_int(2)
#define RESUMED_IO Val_int(3)

#define TAG_RESUMED_SELECT 0
#define TAG_RESUMED_WAIT 1

#define NO_FDS Val_unit
#define NO_DELAY Val_unit
#define NO_JOINING Val_unit
#define NO_WAITPID Val_int(0)

#define DELAY_INFTY 1E30        /* +infty, for this purpose */

/* The thread currently active */
//static caml_thread_t curr_thread = NULL;
/* Identifier for next thread creation */
//static value next_ident = Val_int(0);

#define Assign(dst,src) caml_modify_r(ctx, (value *)&(dst), (value)(src))

/* Scan the stacks of the other threads */

static void (*prev_scan_roots_hook) (scanning_action);

static void thread_scan_roots(scanning_action action)
{
  INIT_CAML_R;
  caml_thread_t th, start;

  /* Scan all active descriptors */
  start = curr_thread;
  (*action)(ctx, (value) curr_thread, (value *) &curr_thread);
  /* Don't scan curr_thread->sp, this has already been done.
     Don't scan local roots either, for the same reason. */
  for (th = start->next; th != start; th = th->next) {
    caml_do_local_roots_r(ctx, action, th->sp, th->stack_high, NULL);
  }
  /* Hook */
  if (prev_scan_roots_hook != NULL) (*prev_scan_roots_hook)(action);
}

/// Ugly and experimental: BEGIN --Luca Saiu REENTRANTRUNTIME
static void caml_vmthreads_channel_mutex_free(struct channel *c){
  free(c->mutex);
  INIT_CAML_R; DDUMP("destroying the channel at %p, with fd %i", c, c->fd);
}
value thread_yield_r(CAML_R, value unit);
static void caml_vmthreads_channel_mutex_lock(struct channel *c){
  pthread_mutex_t *mutex_pointer;
caml_acquire_channel_lock();
  mutex_pointer = c->mutex;
  if (mutex_pointer == NULL) {
    mutex_pointer = caml_stat_alloc(sizeof(pthread_mutex_t));
    caml_initialize_mutex(mutex_pointer);
    c->mutex = mutex_pointer;
  }
caml_release_channel_lock();
  INIT_CAML_R;
  QDUMP("trying to lock channel %p with fd %i", c, c->fd);
  int iteration_no = 0;
  while(pthread_mutex_trylock(mutex_pointer) != 0){
    iteration_no ++;
    thread_yield_r(ctx, Val_unit);
  }
  ctx->last_locked_channel = c;
  if(iteration_no > 0)
    QDUMP("Got the channel with fd %i after %i failed attempts...", (int)c->fd, iteration_no);
}
static void caml_vmthreads_channel_mutex_unlock(struct channel *c){
  INIT_CAML_R;
  ctx->last_locked_channel = NULL;
  pthread_mutex_unlock(c->mutex);
  QDUMP("unlocked channel %p with fd %i", c, c->fd);
}
static void caml_vmthreads_channel_mutex_unlock_exn(void){
  INIT_CAML_R;
  struct channel *the_channel_to_unlock = ctx->last_locked_channel;
  DUMP("I have to unlock %p", the_channel_to_unlock);
  if(the_channel_to_unlock != NULL){
    ctx->last_locked_channel = NULL;
    caml_vmthreads_channel_mutex_unlock(the_channel_to_unlock);
  }
}
/// Ugly and experimental: END --Luca Saiu REENTRANTRUNTIME

/* Forward declarations for async I/O handling */

static int stdin_initial_status, stdout_initial_status, stderr_initial_status;
static void thread_restore_std_descr(void);

/* Initialize the thread machinery for the current context: */

static void caml_thread_initialize_for_current_context_r(CAML_R){
  /* Protect against repeated initialization (PR#1325) */
  if (curr_thread != NULL) return;

  /* Create a descriptor for the current thread */
  curr_thread =
    (caml_thread_t) caml_alloc_shr_r(ctx, sizeof(struct caml_thread_struct) / sizeof(value), 0);
  curr_thread->ident = next_ident;
  next_ident = Val_int(Int_val(next_ident) + 1);
  curr_thread->next = curr_thread;
  curr_thread->prev = curr_thread;
  curr_thread->stack_low = caml_stack_low;
  curr_thread->stack_high = caml_stack_high;
  curr_thread->stack_threshold = caml_stack_threshold;
  curr_thread->sp = caml_extern_sp;
  curr_thread->trapsp = caml_trapsp;
  curr_thread->backtrace_pos = Val_int(caml_backtrace_pos);
  curr_thread->backtrace_buffer = caml_backtrace_buffer;
  caml_initialize_r(ctx, &curr_thread->backtrace_last_exn, caml_backtrace_last_exn);
  curr_thread->status = RUNNABLE;
  curr_thread->fd = Val_int(0);
  curr_thread->readfds = NO_FDS;
  curr_thread->writefds = NO_FDS;
  curr_thread->exceptfds = NO_FDS;
  curr_thread->delay = NO_DELAY;
  curr_thread->joining = NO_JOINING;
  curr_thread->waitpid = NO_WAITPID;
  curr_thread->retval = Val_unit;
  curr_thread->last_locked_channel = ctx->last_locked_channel;
}

/* Initialize the global thread machinery; this has to be called once,
   and not for each context. */
value thread_initialize_r(CAML_R, value unit)       /* ML */
{
  DUMP("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ INITIALIZED VMTHREADS");
  /* It's not inconceivable that C libraries attempt to initialize
     threads more than once: */
  static int already_initialized = 0;
  if(already_initialized){
    caml_initialize_context_thread_support_r(ctx);
    return Val_unit;
  }
  else
    already_initialized = 1;

  /* Define the way of initializing per-context thread support: */
  caml_set_caml_initialize_context_thread_support_r(caml_thread_initialize_for_current_context_r);

  /* Initialize GC */
  prev_scan_roots_hook = scan_roots_hook;
  caml_scan_roots_hook = thread_scan_roots;

  /* Set standard file descriptors to non-blocking mode */
  stdin_initial_status = fcntl(0, F_GETFL);
  stdout_initial_status = fcntl(1, F_GETFL);
  stderr_initial_status = fcntl(2, F_GETFL);
  if (stdin_initial_status != -1)
    fcntl(0, F_SETFL, stdin_initial_status | O_NONBLOCK);
  if (stdout_initial_status != -1)
    fcntl(1, F_SETFL, stdout_initial_status | O_NONBLOCK);
  if (stderr_initial_status != -1)
    fcntl(2, F_SETFL, stderr_initial_status | O_NONBLOCK);
  /* Register an at-exit function to restore the standard file descriptors */
  atexit(thread_restore_std_descr);

  /* Add channel locking functions suitable for vmthreads: */
  caml_channel_mutex_free = caml_vmthreads_channel_mutex_free;
  caml_channel_mutex_lock = caml_vmthreads_channel_mutex_lock;
  caml_channel_mutex_unlock = caml_vmthreads_channel_mutex_unlock;
  caml_channel_mutex_unlock_exn = caml_vmthreads_channel_mutex_unlock_exn;

  /* Now also initialize per-context support for the current context, which
     is presumably the main one: */
  caml_initialize_context_thread_support_r(ctx);
  return Val_unit;
}

/* Initialize the interval timer used for preemption */

value thread_initialize_preemption_r(CAML_R, value unit)     /* ML */
{
  struct itimerval timer;

  timer.it_interval.tv_sec = 0;
  timer.it_interval.tv_usec = Thread_timeout;
  timer.it_value = timer.it_interval;
  setitimer(ITIMER_VIRTUAL, &timer, NULL);
  return Val_unit;
}

/* Create a thread */

value thread_new_r(CAML_R, value clos)          /* ML */
{
  caml_thread_t th;

  /* The context will have one more user: */
  caml_pin_context_r(ctx);

  /* Allocate the thread and its stack */
  Begin_root(clos);
  th = (caml_thread_t) caml_alloc_shr_r(ctx, sizeof(struct caml_thread_struct)
                                   / sizeof(value), 0);
  End_roots();
  th->ident = next_ident;
  next_ident = Val_int(Int_val(next_ident) + 1);
  th->stack_low = (value *) stat_alloc(Thread_stack_size);
  th->stack_high = th->stack_low + Thread_stack_size / sizeof(value);
  th->stack_threshold = th->stack_low + Stack_threshold / sizeof(value);
  th->sp = th->stack_high;
  th->trapsp = th->stack_high;
  /* Set up a return frame that pretends we're applying the function to ().
     This way, the next RETURN instruction will run the function. */
  th->sp -= 5;
  th->sp[0] = Val_unit;         /* dummy local to be popped by RETURN 1 */
  th->sp[1] = (value) Code_val(clos);
  th->sp[2] = clos;
  th->sp[3] = Val_long(0);      /* no extra args */
  th->sp[4] = Val_unit;         /* the () argument */
  /* Fake a C call frame */
  th->sp--;
  th->sp[0] = Val_unit;         /* a dummy environment */
  /* Finish initialization of th */
  th->backtrace_pos = Val_int(0);
  th->backtrace_buffer = NULL;
  th->backtrace_last_exn = Val_unit;
  /* The thread is initially runnable */
  th->status = RUNNABLE;
  th->fd = Val_int(0);
  th->readfds = NO_FDS;
  th->writefds = NO_FDS;
  th->exceptfds = NO_FDS;
  th->delay = NO_DELAY;
  th->joining = NO_JOINING;
  th->waitpid = NO_WAITPID;
  th->retval = Val_unit;
  /* Insert thread in doubly linked list of threads */
  th->prev = curr_thread->prev;
  th->next = curr_thread;
  th->last_locked_channel = ctx->last_locked_channel;
  Assign(curr_thread->prev->next, th);
  Assign(curr_thread->prev, th);
  /* Return thread */
  return (value) th;
}

/* Return the thread identifier */

value thread_id(value th)             /* ML */
{
  return ((caml_thread_t)th)->ident;
}

/* Return the current time as a floating-point number */

static double timeofday(void)
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (double) tv.tv_sec + (double) tv.tv_usec * 1e-6;
}

/* Find a runnable thread and activate it */

#define FOREACH_THREAD(x) x = curr_thread; do { x = x->next;
#define END_FOREACH(x) } while (x != curr_thread)

static value alloc_process_status_r(CAML_R, int pid, int status);
static void add_fdlist_to_set(value fdl, fd_set *set);
static value inter_fdlist_set_r(CAML_R, value fdl, fd_set *set, int *count);
static void find_bad_fd(int fd, fd_set *set);
static void find_bad_fds(value fdl, fd_set *set);

static value schedule_thread_r(CAML_R)
{
  caml_thread_t run_thread, th;
  fd_set readfds, writefds, exceptfds;
  double delay, now;
  int need_select, need_wait;

  /* Don't allow preemption during a callback */
  if (callback_depth > 1) return curr_thread->retval;

  /* Save the status of the current thread */
  curr_thread->stack_low = caml_stack_low;
  curr_thread->stack_high = caml_stack_high;
  curr_thread->stack_threshold = caml_stack_threshold;
  curr_thread->sp = caml_extern_sp;
  curr_thread->trapsp = caml_trapsp;
  curr_thread->backtrace_pos = Val_int(caml_backtrace_pos);
  curr_thread->backtrace_buffer = caml_backtrace_buffer;
  caml_modify_r (ctx, &curr_thread->backtrace_last_exn, caml_backtrace_last_exn);
  curr_thread->last_locked_channel = ctx->last_locked_channel;

try_again:
  /* Find if a thread is runnable.
     Build fdsets and delay for select.
     See if some join or wait operations succeeded. */
  run_thread = NULL;
  FD_ZERO(&readfds);
  FD_ZERO(&writefds);
  FD_ZERO(&exceptfds);
  delay = DELAY_INFTY;
  now = -1.0;
  need_select = 0;
  need_wait = 0;

  FOREACH_THREAD(th)
    if (th->status <= SUSPENDED) continue;

    if (th->status & (BLOCKED_READ - 1)) {
      FD_SET(Int_val(th->fd), &readfds);
      need_select = 1;
    }
    if (th->status & (BLOCKED_WRITE - 1)) {
      FD_SET(Int_val(th->fd), &writefds);
      need_select = 1;
    }
    if (th->status & (BLOCKED_SELECT - 1)) {
      add_fdlist_to_set(th->readfds, &readfds);
      add_fdlist_to_set(th->writefds, &writefds);
      add_fdlist_to_set(th->exceptfds, &exceptfds);
      need_select = 1;
    }
    if (th->status & (BLOCKED_DELAY - 1)) {
      double th_delay;
      if (now < 0.0) now = timeofday();
      th_delay = Double_val(th->delay) - now;
      if (th_delay <= 0) {
        th->status = RUNNABLE;
        Assign(th->retval,RESUMED_DELAY);
      } else {
        if (th_delay < delay) delay = th_delay;
      }
    }
    if (th->status & (BLOCKED_JOIN - 1)) {
      if (((caml_thread_t)(th->joining))->status == KILLED) {
        th->status = RUNNABLE;
        Assign(th->retval, RESUMED_JOIN);
      }
    }
    if (th->status & (BLOCKED_WAIT - 1)) {
      int status, pid;
      pid = waitpid(Int_val(th->waitpid), &status, WNOHANG);
      if (pid > 0) {
        th->status = RUNNABLE;
        Assign(th->retval, alloc_process_status_r(ctx, pid, status));
      } else {
        need_wait = 1;
      }
    }
  END_FOREACH(th);

  /* Find if a thread is runnable. */
  run_thread = NULL;
  FOREACH_THREAD(th)
    if (th->status == RUNNABLE) { run_thread = th; break; }
  END_FOREACH(th);

  /* Do the select if needed */
  if (need_select || run_thread == NULL) {
    struct timeval delay_tv, * delay_ptr;
    int retcode;
    /* If a thread is blocked on wait, don't block forever */
    if (need_wait && delay > Thread_timeout * 1e-6) {
      delay = Thread_timeout * 1e-6;
    }
    /* Convert delay to a timeval */
    /* If a thread is runnable, just poll */
    if (run_thread != NULL) {
      delay_tv.tv_sec = 0;
      delay_tv.tv_usec = 0;
      delay_ptr = &delay_tv;
    }
    else if (delay != DELAY_INFTY) {
      delay_tv.tv_sec = (unsigned int) delay;
      delay_tv.tv_usec = (delay - (double) delay_tv.tv_sec) * 1E6;
      delay_ptr = &delay_tv;
    }
    else {
      delay_ptr = NULL;
    }
    caml_enter_blocking_section_r(ctx);
    retcode = select(FD_SETSIZE, &readfds, &writefds, &exceptfds, delay_ptr);
    caml_leave_blocking_section_r(ctx);
    if (retcode == -1)
      switch (errno) {
      case EINTR:
        break;
      case EBADF:
        /* One of the descriptors in the sets was closed or is bad.
           Find it using fstat() and wake up the threads waiting on it
           so that they'll get an error when operating on it. */
        FOREACH_THREAD(th)
          if (th->status & (BLOCKED_READ - 1)) {
            find_bad_fd(Int_val(th->fd), &readfds);
          }
          if (th->status & (BLOCKED_WRITE - 1)) {
            find_bad_fd(Int_val(th->fd), &writefds);
          }
          if (th->status & (BLOCKED_SELECT - 1)) {
            find_bad_fds(th->readfds, &readfds);
            find_bad_fds(th->writefds, &writefds);
            find_bad_fds(th->exceptfds, &exceptfds);
          }
        END_FOREACH(th);
        retcode = FD_SETSIZE;
        break;
      default:
        caml_sys_error_r(ctx, NO_ARG);
      }
    if (retcode > 0) {
      /* Some descriptors are ready.
         Mark the corresponding threads runnable. */
      FOREACH_THREAD(th)
        if (retcode <= 0) break;
        if ((th->status & (BLOCKED_READ - 1)) &&
            FD_ISSET(Int_val(th->fd), &readfds)) {
          Assign(th->retval, RESUMED_IO);
          th->status = RUNNABLE;
          if (run_thread == NULL) run_thread = th; /* Found one. */
          /* Wake up only one thread per fd */
          FD_CLR(Int_val(th->fd), &readfds);
          retcode--;
        }
        if ((th->status & (BLOCKED_WRITE - 1)) &&
            FD_ISSET(Int_val(th->fd), &writefds)) {
          Assign(th->retval, RESUMED_IO);
          th->status = RUNNABLE;
          if (run_thread == NULL) run_thread = th; /* Found one. */
          /* Wake up only one thread per fd */
          FD_CLR(Int_val(th->fd), &readfds);
          retcode--;
        }
        if (th->status & (BLOCKED_SELECT - 1)) {
          value r = Val_unit, w = Val_unit, e = Val_unit;
          Begin_roots3(r,w,e)
            r = inter_fdlist_set_r(ctx, th->readfds, &readfds, &retcode);
            w = inter_fdlist_set_r(ctx, th->writefds, &writefds, &retcode);
            e = inter_fdlist_set_r(ctx, th->exceptfds, &exceptfds, &retcode);
            if (r != NO_FDS || w != NO_FDS || e != NO_FDS) {
              value retval = caml_alloc_small_r(ctx, 3, TAG_RESUMED_SELECT);
              Field(retval, 0) = r;
              Field(retval, 1) = w;
              Field(retval, 2) = e;
              Assign(th->retval, retval);
              th->status = RUNNABLE;
              if (run_thread == NULL) run_thread = th; /* Found one. */
            }
          End_roots();
        }
      END_FOREACH(th);
    }
    /* If we get here with run_thread still NULL, one of the following
       may have happened:
       - a delay has expired
       - a wait() needs to be polled again
       - the select() failed (e.g. was interrupted)
       In these cases, we go through the loop once more to make the
       corresponding threads runnable. */
    if (run_thread == NULL &&
        (delay != DELAY_INFTY || need_wait || retcode == -1))
      goto try_again;
  }

  /* If we haven't something to run at that point, we're in big trouble. */
  if (run_thread == NULL) caml_invalid_argument_r(ctx, "Thread: deadlock");

  /* Free everything the thread was waiting on */
  Assign(run_thread->readfds, NO_FDS);
  Assign(run_thread->writefds, NO_FDS);
  Assign(run_thread->exceptfds, NO_FDS);
  Assign(run_thread->delay, NO_DELAY);
  Assign(run_thread->joining, NO_JOINING);
  run_thread->waitpid = NO_WAITPID;

  /* Activate the thread */
  curr_thread = run_thread;
  caml_stack_low = curr_thread->stack_low;
  caml_stack_high = curr_thread->stack_high;
  caml_stack_threshold = curr_thread->stack_threshold;
  caml_extern_sp = curr_thread->sp;
  caml_trapsp = curr_thread->trapsp;
  caml_backtrace_pos = Int_val(curr_thread->backtrace_pos);
  caml_backtrace_buffer = curr_thread->backtrace_buffer;
  caml_backtrace_last_exn = curr_thread->backtrace_last_exn;
  ctx->last_locked_channel = curr_thread->last_locked_channel;
  return curr_thread->retval;
}

/* Since context switching is not allowed in callbacks, a thread that
   blocks during a callback is a deadlock. */

static void check_callback_r(CAML_R)
{
  if (caml_callback_depth > 1)
    caml_fatal_error("Thread: deadlock during callback");
}

/* Reschedule without suspending the current thread */

value thread_yield_r(CAML_R, value unit)        /* ML */
{
  Assert(curr_thread != NULL);
  Assign(curr_thread->retval, Val_unit);
  return schedule_thread_r(ctx);
}

/* Honor an asynchronous request for re-scheduling */

static void thread_reschedule(void) /* Don't change the prototype: this is used as a hook --Luca Saiu REENTRANTRUNTIME */
{
  INIT_CAML_R;
  value accu;

  Assert(curr_thread != NULL);
  /* Pop accu from event frame, making it look like a C_CALL frame
     followed by a RETURN frame */
  accu = *extern_sp++;
  /* Reschedule */
  Assign(curr_thread->retval, accu);
  accu = schedule_thread_r(ctx);
  /* Push accu below C_CALL frame so that it looks like an event frame */
  *--extern_sp = accu;
}

/* Request a re-scheduling as soon as possible */

value thread_request_reschedule_r(CAML_R, value unit)    /* ML */
{
  caml_async_action_hook = thread_reschedule;
  caml_something_to_do = 1;
  return Val_unit;
}

/* Suspend the current thread */

value thread_sleep_r(CAML_R, value unit)        /* ML */
{
  Assert(curr_thread != NULL);
  check_callback_r(ctx);
  curr_thread->status = SUSPENDED;
  return schedule_thread_r(ctx);
}

/* Suspend the current thread on a read() or write() request */

static value thread_wait_rw_r(CAML_R, int kind, value fd)
{
  /* Don't do an error if we're not initialized yet
     (we can be called from thread-safe Pervasives before initialization),
     just return immediately. */
  if (curr_thread == NULL) return RESUMED_WAKEUP;
  /* As a special case, if we're in a callback, don't fail but block
     the whole process till I/O is possible */
  if (caml_callback_depth > 1) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(Int_val(fd), &fds);
    switch(kind) {
      case BLOCKED_READ: select(FD_SETSIZE, &fds, NULL, NULL, NULL); break;
      case BLOCKED_WRITE: select(FD_SETSIZE, NULL, &fds, NULL, NULL); break;
    }
    return RESUMED_IO;
  } else {
    curr_thread->fd = fd;
    curr_thread->status = kind;
    return schedule_thread_r(ctx);
  }
}

value thread_wait_read_r(CAML_R, value fd) /* ML */
{
  return thread_wait_rw_r(ctx, BLOCKED_READ, fd);
}

value thread_wait_write_r(CAML_R, value fd) /* ML */
{
  return thread_wait_rw_r(ctx, BLOCKED_WRITE, fd);
}

/* Suspend the current thread on a read() or write() request with timeout */

static value thread_wait_timed_rw_r(CAML_R, int kind, value arg)
{
  double date;

  check_callback_r(ctx);
  curr_thread->fd = Field(arg, 0);
  date = timeofday() + Double_val(Field(arg, 1));
  Assign(curr_thread->delay, caml_copy_double_r(ctx, date));
  curr_thread->status = kind | BLOCKED_DELAY;
  return schedule_thread_r(ctx);
}

value thread_wait_timed_read_r(CAML_R, value arg) /* ML */
{
  return thread_wait_timed_rw_r(ctx, BLOCKED_READ, arg);
}

value thread_wait_timed_write_r(CAML_R, value arg) /* ML */
{
  return thread_wait_timed_rw_r(ctx, BLOCKED_WRITE, arg);
}

/* Suspend the current thread on a select() request */

value thread_select_r(CAML_R, value arg)        /* ML */
{
  double date;
  check_callback_r(ctx);
  Assign(curr_thread->readfds, Field(arg, 0));
  Assign(curr_thread->writefds, Field(arg, 1));
  Assign(curr_thread->exceptfds, Field(arg, 2));
  date = Double_val(Field(arg, 3));
  if (date >= 0.0) {
    date += timeofday();
    Assign(curr_thread->delay, caml_copy_double_r(ctx, date));
    curr_thread->status = BLOCKED_SELECT | BLOCKED_DELAY;
  } else {
    curr_thread->status = BLOCKED_SELECT;
  }
  return schedule_thread_r(ctx);
}

/* Primitives to implement suspension on buffered channels */

value thread_inchan_ready(value vchan) /* ML */
{
  struct channel * chan = Channel(vchan);
  return Val_bool(chan->curr < chan->max);
}

value thread_outchan_ready(value vchan, value vsize) /* ML */
{
  struct channel * chan = Channel(vchan);
  intnat size = Long_val(vsize);
  /* Negative size means we want to flush the buffer entirely */
  if (size < 0) {
    return Val_bool(chan->curr == chan->buff);
  } else {
    int free = chan->end - chan->curr;
    if (chan->curr == chan->buff)
      return Val_bool(size < free);
    else
      return Val_bool(size <= free);
  }
}

/* Suspend the current thread for some time */

CAMLprim value thread_delay_r(CAML_R, value time)          /* ML */ // FIXME: remove CAMLprim after testing
{
  double date = timeofday() + Double_val(time);
  Assert(curr_thread != NULL);
  check_callback_r(ctx);
  curr_thread->status = BLOCKED_DELAY;
  Assign(curr_thread->delay, caml_copy_double_r(ctx, date));
  return schedule_thread_r(ctx);
}

/* Suspend the current thread until another thread terminates */

value thread_join_r(CAML_R, value th)          /* ML */
{
  check_callback_r(ctx);
  Assert(curr_thread != NULL);
  if (((caml_thread_t)th)->status == KILLED) return Val_unit;
  curr_thread->status = BLOCKED_JOIN;
  Assign(curr_thread->joining, th);
  return schedule_thread_r(ctx);
}

/* Suspend the current thread until a Unix process exits */

value thread_wait_pid_r(CAML_R, value pid)          /* ML */
{
  Assert(curr_thread != NULL);
  check_callback_r(ctx);
  curr_thread->status = BLOCKED_WAIT;
  curr_thread->waitpid = pid;
  return schedule_thread_r(ctx);
}

/* Reactivate another thread */

value thread_wakeup_r(CAML_R, value thread)     /* ML */
{
  caml_thread_t th = (caml_thread_t) thread;
  switch (th->status) {
  case SUSPENDED:
    th->status = RUNNABLE;
    Assign(th->retval, RESUMED_WAKEUP);
    break;
  case KILLED:
    failwith("Thread.wakeup: killed thread");
  default:
    failwith("Thread.wakeup: thread not suspended");
  }
  return Val_unit;
}

/* Return the current thread */

value thread_self_r(CAML_R, value unit)         /* ML */
{
  Assert(curr_thread != NULL);
  return (value) curr_thread;
}

/* Kill a thread */

value thread_kill_r(CAML_R, value thread)       /* ML */
{
  value retval = Val_unit;
  caml_thread_t th = (caml_thread_t) thread;
  if (th->status == KILLED) failwith("Thread.kill: killed thread");
  /* Don't paint ourselves in a corner */
  if (th == th->next) failwith("Thread.kill: cannot kill the last thread");
  /* This thread is no longer waiting on anything */
  th->status = KILLED;
  /* If this is the current thread, activate another one */
  if (th == curr_thread) {
    Begin_root(thread);
    retval = schedule_thread_r(ctx);
    th = (caml_thread_t) thread;
    End_roots();
  }
  /* Remove thread from the doubly-linked list */
  Assign(th->prev->next, th->next);
  Assign(th->next->prev, th->prev);
  /* Free its resources */
  stat_free((char *) th->stack_low);
  th->stack_low = NULL;
  th->stack_high = NULL;
  th->stack_threshold = NULL;
  th->sp = NULL;
  th->trapsp = NULL;
  if (th->backtrace_buffer != NULL) {
    free(th->backtrace_buffer);
    th->backtrace_buffer = NULL;
  }
  th->last_locked_channel = NULL; // just to ease debugging: there's nothing to free here

  /* There's one less user for this context: */
  caml_unpin_context_r(ctx);

  return retval;
}

/* Print uncaught exception and backtrace */

value thread_uncaught_exception_r(CAML_R, value exn)  /* ML */
{
  char * msg = caml_format_exception_r(ctx, exn);
  fprintf(stderr, "Thread %d killed on uncaught exception %s\n",
          Int_val(curr_thread->ident), msg);
  free(msg);
  if (backtrace_active) caml_print_exception_backtrace_r(ctx);
  fflush(stderr);
  return Val_unit;
}

/* Set a list of file descriptors in a fdset */

static void add_fdlist_to_set(value fdl, fd_set *set)
{
  for (/*nothing*/; fdl != NO_FDS; fdl = Field(fdl, 1)) {
    int fd = Int_val(Field(fdl, 0));
    /* Ignore funky file descriptors, which can cause crashes */
    if (fd >= 0 && fd < FD_SETSIZE) FD_SET(fd, set);
  }
}

/* Build the intersection of a list and a fdset (the list of file descriptors
   which are both in the list and in the fdset). */

static value inter_fdlist_set_r(CAML_R, value fdl, fd_set *set, int *count)
{
  value res = Val_unit;
  value cons;

  Begin_roots2(fdl, res);
    for (res = NO_FDS; fdl != NO_FDS; fdl = Field(fdl, 1)) {
      int fd = Int_val(Field(fdl, 0));
      if (FD_ISSET(fd, set)) {
        cons = caml_alloc_small_r(ctx, 2, 0);
        Field(cons, 0) = Val_int(fd);
        Field(cons, 1) = res;
        res = cons;
        FD_CLR(fd, set); /* wake up only one thread per fd ready */
        (*count)--;
      }
    }
  End_roots();
  return res;
}

/* Find closed file descriptors in a waiting list and set them to 1 in
   the given fdset */

static void find_bad_fd(int fd, fd_set *set)
{
  struct stat s;
  if (fd >= 0 && fd < FD_SETSIZE && fstat(fd, &s) == -1 && errno == EBADF)
    FD_SET(fd, set);
}

static void find_bad_fds(value fdl, fd_set *set)
{
  for (/*nothing*/; fdl != NO_FDS; fdl = Field(fdl, 1))
    find_bad_fd(Int_val(Field(fdl, 0)), set);
}

/* Auxiliary function for allocating the result of a waitpid() call */

#if !(defined(WIFEXITED) && defined(WEXITSTATUS) && defined(WIFSTOPPED) && \
      defined(WSTOPSIG) && defined(WTERMSIG))
/* Assume old-style V7 status word */
#define WIFEXITED(status) (((status) & 0xFF) == 0)
#define WEXITSTATUS(status) (((status) >> 8) & 0xFF)
#define WIFSTOPPED(status) (((status) & 0xFF) == 0xFF)
#define WSTOPSIG(status) (((status) >> 8) & 0xFF)
#define WTERMSIG(status) ((status) & 0x3F)
#endif

#define TAG_WEXITED 0
#define TAG_WSIGNALED 1
#define TAG_WSTOPPED 2

static value alloc_process_status_r(CAML_R, int pid, int status)
{
  value st, res;

  if (WIFEXITED(status)) {
    st = caml_alloc_small_r(ctx, 1, TAG_WEXITED);
    Field(st, 0) = Val_int(WEXITSTATUS(status));
  }
  else if (WIFSTOPPED(status)) {
    st = caml_alloc_small_r(ctx, 1, TAG_WSTOPPED);
    Field(st, 0) = Val_int(WSTOPSIG(status));
  }
  else {
    st = caml_alloc_small_r(ctx, 1, TAG_WSIGNALED);
    Field(st, 0) = Val_int(WTERMSIG(status));
  }
  Begin_root(st);
    res = caml_alloc_small_r(ctx, 2, TAG_RESUMED_WAIT);
    Field(res, 0) = Val_int(pid);
    Field(res, 1) = st;
  End_roots();
  return res;
}

/* Restore the standard file descriptors to their initial state */

static void thread_restore_std_descr(void)
{
  if (stdin_initial_status != -1) fcntl(0, F_SETFL, stdin_initial_status);
  if (stdout_initial_status != -1) fcntl(1, F_SETFL, stdout_initial_status);
  if (stderr_initial_status != -1) fcntl(2, F_SETFL, stderr_initial_status);
}
