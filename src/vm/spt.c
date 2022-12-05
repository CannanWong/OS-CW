#include "vm/spt.h"
#include <hash.h>
#include <stdio.h>
#include <string.h>
#include "devices/swap.h"
#include "filesys/file.h"
#include "userprog/exception.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "userprog/syscall.h"
#include "threads/interrupt.h"
#include "threads/pte.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "vm/frame.h"

unsigned spt_hash (const struct hash_elem *, void *);
bool spt_less (const struct hash_elem *, 
                      const struct hash_elem *,
                      void *);
static struct hash *cur_spt (void);
static void spt_destroy_single (struct hash_elem *, void *);
static void zero_from (void *, int);
static bool read_segment_from_file (struct spt_entry *, void *);

bool
spt_init (struct thread *t) {
  return hash_init (&t->spt, spt_hash, spt_less, NULL);
}

bool
spt_insert (struct spt_entry *entry) {
  struct hash_elem *e = hash_replace (cur_spt(), &entry->spt_elem);
  if (e) {
    struct spt_entry *replaced = hash_entry (e, struct spt_entry, spt_elem);
    free (replaced);
  }
  return e != NULL;
}

/* Search for spt_entry with upage as key,
  return NULL if not found. */
struct spt_entry *
spt_lookup (void *upage) {
  struct spt_entry dummy;
  dummy.upage = upage;
  struct hash_elem *e;
  e = hash_find (cur_spt(), &dummy.spt_elem);
  return e == NULL ? NULL : hash_entry (e, struct spt_entry, spt_elem);
}

bool
spt_remove (void *upage) {
  struct spt_entry dummy;
  dummy.upage = upage;
  struct hash_elem *e = hash_delete (cur_spt(), &dummy.spt_elem);
  if (e) {
    //Possibly freeing any memory allocated to spt_entry variables...
    //spt_destroy_single?
    free (hash_entry (e, struct spt_entry, spt_elem));
  }
  return e != NULL;
}

static void
spt_destroy_single (struct hash_elem *e, void *aux UNUSED) {
  //Do something when freeing all
}

void
spt_destroy () {
  hash_destroy (cur_spt(), spt_destroy_single);
}

bool
lazy_load (struct file *file, off_t ofs, uint8_t *upage,
          uint32_t read_bytes, uint32_t zero_bytes, bool writable,
          enum page_location location) {
  /*
  printf("loading ");
  printf(writable? "w " : "n/w ");
  printf("segment at ofs %d to upage %p,\n", ofs, upage);
  printf("reading in %d and zeroing %d bytes...\n\n", read_bytes, zero_bytes);
  */
  while (read_bytes > 0 || zero_bytes > 0) 
  {
    /* Calculate how to fill this page.
       We will read PAGE_READ_BYTES bytes from FILE
       and zero the final PAGE_ZERO_BYTES bytes. */
    size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
    size_t page_zero_bytes = PGSIZE - page_read_bytes;
    
//    /* Check if virtual page already allocated */
//    struct thread *t = thread_current ();
//    uint8_t *kpage = pagedir_get_page (t->pagedir, upage);
//    
//    if (kpage == NULL){
//      
//      /* Get a new page of memory. */
//      kpage = get_frame (PAL_USER, upage);
//      if (kpage == NULL){
//        return false;
//      }
//      
//      /* Add the page to the process's address space. */
//      if (!install_page (upage, kpage, writable)) 
//      {
//        free_frame (kpage);
//        return false; 
//      }     
//      
//    } else {
//      
//      /* Check if writable flag for the page should be updated */
//      if (writable && !pagedir_is_writable (t->pagedir, upage)){
//        pagedir_set_writable (t->pagedir, upage, writable); 
//      }
//      
//    }
//
//    /* Load data into the page. */
//    if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes){
//      return false; 
//    }
//    memset (kpage + page_read_bytes, 0, page_zero_bytes);

    /* New load_segment with lazy loading. */
    struct spt_entry *entry = spt_lookup (upage);
    if (!entry) {
      //printf("load seg: creating entry for %p\n", upage);
      // No previous entries in SPT, creates one and insert after assign args
      entry = (struct spt_entry *) malloc (sizeof (struct spt_entry));
      if (entry == NULL) {
        return false;
      }
      entry->location = location;
      entry->file = file;
      entry->ofs = ofs;
      entry->rbytes = page_read_bytes;
      entry->zbytes = page_zero_bytes;
      entry->upage = upage;
      entry->writable = writable;
      spt_insert (entry);
    } else {
      // Previous entry present, update SPT meta-data (load_segment).
      //printf("Previous entry present, update SPT meta-data.\n");
      if (page_read_bytes != entry->rbytes) {
        uint32_t old_rb = entry->rbytes;
        uint32_t old_zb = entry->zbytes;
        entry->rbytes = page_read_bytes;
        entry->zbytes = page_zero_bytes;
        //printf("rb old vs new: %u: %u\n", old_rb, entry->rbytes);
        //printf("zb old vs new: %u: %u\n", old_zb, entry->zbytes);
      }
      if (writable)
        entry->writable = writable;
    }
    /* Advance. */
    read_bytes -= page_read_bytes;
    zero_bytes -= page_zero_bytes;
    upage += PGSIZE;
    ofs += PGSIZE;
  }
  return true;
}

bool
spt_pf_handler (void *fault_addr, bool not_present, bool write, bool user) {
  void *fault_page = pg_round_down (fault_addr);

  //printf("\n\nexception: fault addr: %p\n", fault_addr);
  /*
  if (ft_search_entry(aligned_fault_page)) {
    printf("spt_pf_handler: frame entry for this upage exists.\n");
  }
  */
  //printf("##### pagedir is %p\n", pagedir_get_page(thread_current()->pagedir, aligned_fault_page));
  struct spt_entry *entry = spt_lookup (fault_page);
  /*
  printf("spt_pf: upage = %p;  ", fault_page);
  if (entry)
    printf(entry->writable? "w\n" : "n/w\n");
  else
    printf("no entry.\n");
  */

  if (entry == NULL || is_kernel_vaddr (fault_addr) || !not_present
    || (write && !entry->writable)) {
    /*
    if (entry == NULL) {
      printf("Can't find entry.\n");
    }
    if (is_kernel_vaddr (fault_addr)) {
      printf("Access kernel addr.\n");
    }
    if (!not_present) {
      printf("Writing r/o page.\n");
    }
    if (write && !entry->writable) {
      printf("Write to FILESYS r-o page.\n");
    }
    if (user) {
      printf("User fault!\n");
    } else {
      printf("Kernel fault!\n");
    }
    */
    return false;
  } else {
    /*allocate frame if frame not previously allocated.*/
    void *frame_pt = get_frame (PAL_USER, entry->upage);
    //printf("frame kpage: %p\n", frame_pt); 
    if (frame_pt == NULL) {
      //printf("Dying due to frame.\n");
      return false;
    } else {
      if (entry->location == FILE_SYS) {
        // Lazy loading..
        /* Either zero-out page,
          or fetch the data into the frame from the file,
          then point PTE to the frame. */
        if (entry->zbytes == PGSIZE) {
          zero_from (frame_pt, PGSIZE);
        } else if (!read_segment_from_file (entry, frame_pt)) {
          //printf("Dying due to read to file.\n");
          return false;
        }
        if (!pagedir_set_page (
            thread_current()->pagedir, fault_page, frame_pt, entry->writable)) {
          //printf("Dying due to setpage.\n");
          return false;
        }
      }
      if (entry->location == SWAP) {
        //Takes information in swap disk thru swap_in
      }
    }
  }
  //printf("---------LAZY LOADING COMPLETE---------\n\n");
  return true;
}

static void
zero_from (void *from, int size) {
  memset (from, 0, PGSIZE);
}

static bool
read_segment_from_file (struct spt_entry *entry, void *frame_pt) {
  //Fetch data into the frame.
  filesys_lock ();
  file_seek (entry->file, entry->ofs);
  bool read_success = 
    entry->rbytes == (uint32_t) file_read (entry->file, frame_pt, entry->rbytes);
  zero_from (frame_pt + entry->rbytes, entry->zbytes);
  filesys_unlock ();
  return read_success;
}

static struct hash *cur_spt () {
  return &thread_current()->spt;
}

unsigned
spt_hash (const struct hash_elem *e, void *aux UNUSED) {
  const struct spt_entry *entry = hash_entry (e, struct spt_entry, spt_elem);
  return hash_int (entry->upage);
}

bool
spt_less (const struct hash_elem *a, const struct hash_elem *b,
          void *aux UNUSED) {
  const struct spt_entry *a_entry = hash_entry (a, struct spt_entry, spt_elem);
  const struct spt_entry *b_entry = hash_entry (b, struct spt_entry, spt_elem);
  return a_entry->upage < b_entry->upage;
}