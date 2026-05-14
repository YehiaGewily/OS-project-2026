#include "userprog/syscall.h"
#include <stdio.h>
#include <string.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "userprog/process.h"
#include "userprog/pagedir.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "devices/shutdown.h"
#include "devices/input.h"

typedef int pid_t;

static void syscall_handler (struct intr_frame *);
static struct lock filesys_lock;

/* ---------- pointer validation ---------- */

/* Verify that UADDR is a valid user virtual address:
   not null, below PHYS_BASE, and mapped. */
static void
validate_ptr (const void *uaddr)
{
  if (uaddr == NULL
      || !is_user_vaddr (uaddr)
      || pagedir_get_page (thread_current ()->pagedir, uaddr) == NULL)
    syscall_exit (-1);
}

/* Validate a buffer of SIZE bytes starting at BUFFER. */
static void
validate_buffer (const void *buffer, unsigned size)
{
  const uint8_t *buf = buffer;
  unsigned i;
  for (i = 0; i < size; i += PGSIZE)
    validate_ptr (buf + i);
  if (size > 0)
    validate_ptr (buf + size - 1);
}

/* Validate a user string (check each byte until null terminator). */
static void
validate_string (const char *str)
{
  validate_ptr (str);
  while (*str != '\0')
    {
      str++;
      validate_ptr (str);
    }
}

/* Read a 32-bit word from user stack at ESP + OFFSET. */
static uint32_t
get_arg (struct intr_frame *f, int offset)
{
  uint32_t *addr = (uint32_t *) f->esp + offset;
  validate_ptr (addr);
  validate_ptr ((uint8_t *) addr + 3);
  return *addr;
}

/* ---------- file descriptor helpers ---------- */

static struct file *
fd_to_file (int fd)
{
  struct thread *cur = thread_current ();
  if (fd < 2 || fd >= MAX_FDS || cur->fd_table == NULL)
    return NULL;
  return cur->fd_table[fd];
}

static int
allocate_fd (struct file *f)
{
  struct thread *cur = thread_current ();
  if (cur->fd_table == NULL)
    {
      cur->fd_table = calloc (MAX_FDS, sizeof (struct file *));
      if (cur->fd_table == NULL)
        return -1;
      cur->next_fd = 2;
    }
  if (cur->next_fd >= MAX_FDS)
    return -1;
  int fd = cur->next_fd;
  cur->fd_table[fd] = f;
  /* Find next free slot. */
  while (cur->next_fd < MAX_FDS && cur->fd_table[cur->next_fd] != NULL)
    cur->next_fd++;
  return fd;
}

/* ---------- syscall_exit (public, called from exception handler too) ---------- */

void
syscall_exit (int status)
{
  struct thread *cur = thread_current ();
  cur->exit_status = status;
  thread_exit ();
}

/* ---------- individual syscall implementations ---------- */

static void
syscall_halt (void)
{
  shutdown_power_off ();
}

static pid_t
syscall_exec (const char *cmd_line)
{
  validate_string (cmd_line);
  lock_acquire (&filesys_lock);
  pid_t pid = (pid_t) process_execute (cmd_line);
  lock_release (&filesys_lock);
  return pid;
}

static int
syscall_wait (pid_t pid)
{
  return process_wait ((tid_t) pid);
}

static bool
syscall_create (const char *file, unsigned initial_size)
{
  validate_string (file);
  lock_acquire (&filesys_lock);
  bool success = filesys_create (file, initial_size);
  lock_release (&filesys_lock);
  return success;
}

static bool
syscall_remove (const char *file)
{
  validate_string (file);
  lock_acquire (&filesys_lock);
  bool success = filesys_remove (file);
  lock_release (&filesys_lock);
  return success;
}

static int
syscall_open (const char *file)
{
  validate_string (file);
  lock_acquire (&filesys_lock);
  struct file *f = filesys_open (file);
  lock_release (&filesys_lock);
  if (f == NULL)
    return -1;
  int fd = allocate_fd (f);
  if (fd == -1)
    file_close (f);
  return fd;
}

static int
syscall_filesize (int fd)
{
  struct file *f = fd_to_file (fd);
  if (f == NULL)
    return -1;
  lock_acquire (&filesys_lock);
  int size = file_length (f);
  lock_release (&filesys_lock);
  return size;
}

static int
syscall_read (int fd, void *buffer, unsigned size)
{
  validate_buffer (buffer, size);
  if (fd == 0)
    {
      /* Read from keyboard. */
      uint8_t *buf = buffer;
      unsigned i;
      for (i = 0; i < size; i++)
        buf[i] = input_getc ();
      return (int) size;
    }
  struct file *f = fd_to_file (fd);
  if (f == NULL)
    return -1;
  lock_acquire (&filesys_lock);
  int bytes_read = file_read (f, buffer, size);
  lock_release (&filesys_lock);
  return bytes_read;
}

static int
syscall_write (int fd, const void *buffer, unsigned size)
{
  validate_buffer (buffer, size);
  if (fd == 1)
    {
      /* Write to console. */
      putbuf (buffer, size);
      return (int) size;
    }
  struct file *f = fd_to_file (fd);
  if (f == NULL)
    return -1;
  lock_acquire (&filesys_lock);
  int bytes_written = file_write (f, buffer, size);
  lock_release (&filesys_lock);
  return bytes_written;
}

static void
syscall_seek (int fd, unsigned position)
{
  struct file *f = fd_to_file (fd);
  if (f == NULL)
    return;
  lock_acquire (&filesys_lock);
  file_seek (f, position);
  lock_release (&filesys_lock);
}

static unsigned
syscall_tell (int fd)
{
  struct file *f = fd_to_file (fd);
  if (f == NULL)
    return 0;
  lock_acquire (&filesys_lock);
  unsigned pos = (unsigned) file_tell (f);
  lock_release (&filesys_lock);
  return pos;
}

static void
syscall_close (int fd)
{
  struct file *f = fd_to_file (fd);
  if (f == NULL)
    return;
  struct thread *cur = thread_current ();
  cur->fd_table[fd] = NULL;
  if (fd < cur->next_fd)
    cur->next_fd = fd;
  lock_acquire (&filesys_lock);
  file_close (f);
  lock_release (&filesys_lock);
}

/* ---------- main dispatcher ---------- */

void
syscall_init (void) 
{
  lock_init (&filesys_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
  validate_ptr (f->esp);

  int syscall_num = (int) get_arg (f, 0);

  switch (syscall_num)
    {
    case SYS_HALT:
      syscall_halt ();
      break;

    case SYS_EXIT:
      syscall_exit ((int) get_arg (f, 1));
      break;

    case SYS_EXEC:
      f->eax = (uint32_t) syscall_exec ((const char *) get_arg (f, 1));
      break;

    case SYS_WAIT:
      f->eax = (uint32_t) syscall_wait ((pid_t) get_arg (f, 1));
      break;

    case SYS_CREATE:
      f->eax = (uint32_t) syscall_create ((const char *) get_arg (f, 1),
                                          (unsigned) get_arg (f, 2));
      break;

    case SYS_REMOVE:
      f->eax = (uint32_t) syscall_remove ((const char *) get_arg (f, 1));
      break;

    case SYS_OPEN:
      f->eax = (uint32_t) syscall_open ((const char *) get_arg (f, 1));
      break;

    case SYS_FILESIZE:
      f->eax = (uint32_t) syscall_filesize ((int) get_arg (f, 1));
      break;

    case SYS_READ:
      f->eax = (uint32_t) syscall_read ((int) get_arg (f, 1),
                                        (void *) get_arg (f, 2),
                                        (unsigned) get_arg (f, 3));
      break;

    case SYS_WRITE:
      f->eax = (uint32_t) syscall_write ((int) get_arg (f, 1),
                                         (const void *) get_arg (f, 2),
                                         (unsigned) get_arg (f, 3));
      break;

    case SYS_SEEK:
      syscall_seek ((int) get_arg (f, 1), (unsigned) get_arg (f, 2));
      break;

    case SYS_TELL:
      f->eax = (uint32_t) syscall_tell ((int) get_arg (f, 1));
      break;

    case SYS_CLOSE:
      syscall_close ((int) get_arg (f, 1));
      break;

    default:
      syscall_exit (-1);
      break;
    }
}
