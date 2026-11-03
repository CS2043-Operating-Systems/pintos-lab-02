#include "userprog/syscall.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall-nr.h>
#include "devices/block.h"
#include "devices/input.h"
#include "devices/shutdown.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"

struct lock filesys_lock;

/* Descriptors are local to a process; 0 and 1 are reserved for I/O. */
struct file_handle
  {
    int fd;
    struct file *file;
    struct list_elem elem;
  };

static void syscall_handler (struct intr_frame *);
static void require_user_range (const void *, size_t, bool);
static uint32_t user_word (const void *);
static uint32_t argument (const struct intr_frame *, unsigned);
static char *user_string (const char *);
static struct file_handle *find_handle (int);
static void close_handle (struct file_handle *);

void
syscall_init (void)
{
  lock_init (&filesys_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/* A bad user pointer terminates only its process, never the kernel. */
void
syscall_exit_process (int status)
{
  thread_current ()->exit_status = status;
  thread_exit ();
}

/* Check every page before an operation can acquire the file-system lock. */
static void
require_user_range (const void *buffer, size_t size, bool writable)
{
  uintptr_t first = (uintptr_t) buffer;
  uintptr_t end = (uintptr_t) PHYS_BASE;
  uintptr_t page;

  if (size == 0)
    return;
  if (first == 0 || first >= end || size > end - first)
    syscall_exit_process (-1);

  page = (uintptr_t) pg_round_down (buffer);
  for (; page < first + size; page += PGSIZE)
    {
      uint32_t *pd = thread_current ()->pagedir;
      if (pagedir_get_page (pd, (const void *) page) == NULL
          || (writable && !pagedir_is_writable (pd, (const void *) page)))
        syscall_exit_process (-1);
    }
}

static uint32_t
user_word (const void *address)
{
  uint32_t value;
  require_user_range (address, sizeof value, false);
  memcpy (&value, address, sizeof value);
  return value;
}

static uint32_t
argument (const struct intr_frame *f, unsigned index)
{
  uintptr_t address = (uintptr_t) f->esp + (index + 1) * sizeof (uint32_t);
  return user_word ((const void *) address);
}

/* Copy a user string before taking locks or starting another thread. */
static char *
user_string (const char *source)
{
  char *copy = palloc_get_page (0);
  size_t i;

  if (copy == NULL)
    return NULL;
  for (i = 0; i < PGSIZE; i++)
    {
      uintptr_t address = (uintptr_t) source + i;
      if (address < (uintptr_t) source)
        break;
      if (address == 0 || address >= (uintptr_t) PHYS_BASE
          || pagedir_get_page (thread_current ()->pagedir,
                               (const void *) address) == NULL)
        {
          palloc_free_page (copy);
          syscall_exit_process (-1);
        }
      copy[i] = source[i];
      if (copy[i] == '\0')
        return copy;
    }
  palloc_free_page (copy);
  return NULL;
}

static struct file_handle *
find_handle (int fd)
{
  struct list *files = &thread_current ()->open_files;
  struct list_elem *e;
  for (e = list_begin (files); e != list_end (files); e = list_next (e))
    {
      struct file_handle *handle = list_entry (e, struct file_handle, elem);
      if (handle->fd == fd)
        return handle;
    }
  return NULL;
}

static void
close_handle (struct file_handle *handle)
{
  list_remove (&handle->elem);
  lock_acquire (&filesys_lock);
  file_close (handle->file);
  lock_release (&filesys_lock);
  free (handle);
}

void
syscall_close_all (void)
{
  struct list *files = &thread_current ()->open_files;
  while (!list_empty (files))
    close_handle (list_entry (list_front (files), struct file_handle, elem));
}

static void
syscall_handler (struct intr_frame *f)
{
  uint32_t number = user_word (f->esp);
  uint32_t fd, address, byte_count;
  uint32_t initial_size, position;
  char *name;
  struct file_handle *handle;

  switch (number)
    {
    case SYS_HALT:
      shutdown_power_off ();
      break;
    case SYS_EXIT:
      syscall_exit_process ((int) argument (f, 0));
      break;
    case SYS_EXEC:
      name = user_string ((const char *) argument (f, 0));
      f->eax = name == NULL ? TID_ERROR : process_execute (name);
      if (name != NULL)
        palloc_free_page (name);
      break;
    case SYS_WAIT:
      f->eax = process_wait ((tid_t) argument (f, 0));
      break;
    case SYS_CREATE:
      /* Read stack arguments before allocating the filename copy. */
      initial_size = argument (f, 1);
      name = user_string ((const char *) argument (f, 0));
      if (name == NULL)
        f->eax = false;
      else
        {
          /* The file system uses signed offsets and rounds up to sectors. */
          f->eax = false;
          if (initial_size <= INT_MAX - BLOCK_SECTOR_SIZE + 1)
            {
              lock_acquire (&filesys_lock);
              f->eax = filesys_create (name, (off_t) initial_size);
              lock_release (&filesys_lock);
            }
          palloc_free_page (name);
        }
      break;
    case SYS_REMOVE:
      name = user_string ((const char *) argument (f, 0));
      if (name == NULL)
        f->eax = false;
      else
        {
          lock_acquire (&filesys_lock);
          f->eax = filesys_remove (name);
          lock_release (&filesys_lock);
          palloc_free_page (name);
        }
      break;
    case SYS_OPEN:
      name = user_string ((const char *) argument (f, 0));
      f->eax = -1;
      if (name == NULL)
        break;
      if (thread_current ()->next_fd == INT_MAX)
        {
          palloc_free_page (name);
          break;
        }
      handle = malloc (sizeof *handle);
      if (handle != NULL)
        {
          lock_acquire (&filesys_lock);
          handle->file = filesys_open (name);
          lock_release (&filesys_lock);
          if (handle->file != NULL)
            {
              handle->fd = thread_current ()->next_fd++;
              list_push_back (&thread_current ()->open_files, &handle->elem);
              f->eax = handle->fd;
            }
          else
            free (handle);
        }
      palloc_free_page (name);
      break;
    case SYS_FILESIZE:
      handle = find_handle ((int) argument (f, 0));
      f->eax = -1;
      if (handle != NULL)
        {
          lock_acquire (&filesys_lock);
          f->eax = file_length (handle->file);
          lock_release (&filesys_lock);
        }
      break;
    case SYS_READ:
      fd = argument (f, 0);
      address = argument (f, 1);
      byte_count = argument (f, 2);
      require_user_range ((void *) address, byte_count, true);
      f->eax = -1;
      if ((int) fd == 0)
        {
          unsigned i;
          for (i = 0; i < byte_count; i++)
            ((uint8_t *) address)[i] = input_getc ();
          f->eax = byte_count;
        }
      else if ((handle = find_handle ((int) fd)) != NULL)
        {
          lock_acquire (&filesys_lock);
          f->eax = file_read (handle->file, (void *) address, byte_count);
          lock_release (&filesys_lock);
        }
      break;
    case SYS_WRITE:
      fd = argument (f, 0);
      address = argument (f, 1);
      byte_count = argument (f, 2);
      require_user_range ((const void *) address, byte_count, false);
      f->eax = -1;
      if ((int) fd == 1)
        {
          putbuf ((const char *) address, byte_count);
          f->eax = byte_count;
        }
      else if ((handle = find_handle ((int) fd)) != NULL)
        {
          lock_acquire (&filesys_lock);
          f->eax = file_write (handle->file, (const void *) address,
                               byte_count);
          lock_release (&filesys_lock);
        }
      break;
    case SYS_SEEK:
      handle = find_handle ((int) argument (f, 0));
      position = argument (f, 1);
      /* An unsigned user offset must not become negative in file_seek. */
      if (position > INT_MAX)
        syscall_exit_process (-1);
      if (handle != NULL)
        {
          lock_acquire (&filesys_lock);
          file_seek (handle->file, (off_t) position);
          lock_release (&filesys_lock);
        }
      break;
    case SYS_TELL:
      handle = find_handle ((int) argument (f, 0));
      f->eax = -1;
      if (handle != NULL)
        {
          lock_acquire (&filesys_lock);
          f->eax = file_tell (handle->file);
          lock_release (&filesys_lock);
        }
      break;
    case SYS_CLOSE:
      handle = find_handle ((int) argument (f, 0));
      if (handle != NULL)
        close_handle (handle);
      break;
    default:
      syscall_exit_process (-1);
    }
}
