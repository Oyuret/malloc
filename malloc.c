#include "brk.h"
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/mman.h>

#define NALLOC 1024                                     /* minimum #units to request */

#ifndef STRATEGY
#define STRATEGY 1
#endif // STRATEGY

typedef long Align;                                     /* for alignment to long boundary */

union header {                                          /* block header */
   struct {
      union header *ptr;                                  /* next block if on free list */
      unsigned size;                                      /* size of this block  - what unit? */
   } s;
   Align x;                                              /* force alignment of blocks */
};

typedef union header Header;

void * best_fit(Header* p, Header* prevp, unsigned nunits);
void * first_fit(Header* p, Header* prevp, unsigned nunits);

static Header base;                                     /* empty list to get started */
static Header *freep = NULL;                            /* start of free list */

/* Print the info on the given Header */
void print(Header* ptr) {
   fprintf(stderr, "%s: %ld\n", "Header adress", (long)ptr);
   fprintf(stderr, "%s: %ud\n", "size", ptr->s.size);
}

/* free: put block ap in the free list */

void free(void * ap) {
   Header *bp, *p;

   if(ap == NULL) return;                                /* Nothing to do */

   bp = (Header *) ap - 1;                               /* point to block header */
   for(p = freep; !(bp > p && bp < p->s.ptr); p = p->s.ptr)
      if(p >= p->s.ptr && (bp > p || bp < p->s.ptr))
         break;                                            /* freed block at atrt or end of arena */

   if(bp + bp->s.size == p->s.ptr) {                     /* join to upper nb */
      bp->s.size += p->s.ptr->s.size;
      bp->s.ptr = p->s.ptr->s.ptr;
   } else
      bp->s.ptr = p->s.ptr;
   if(p + p->s.size == bp) {                             /* join to lower nbr */
      p->s.size += bp->s.size;
      p->s.ptr = bp->s.ptr;
   } else
      p->s.ptr = bp;
   freep = p;
}

/* morecore: ask system for more memory */

#ifdef MMAP

static void * __endHeap = 0;

void * endHeap(void) {
   if(__endHeap == 0) __endHeap = sbrk(0);
   return __endHeap;
}
#endif


static Header *morecore(unsigned nu) {
   void *cp;
   Header *up;
#ifdef MMAP
   unsigned noPages;
   if(__endHeap == 0) __endHeap = sbrk(0);
#endif

   if(nu < NALLOC)
      nu = NALLOC;
#ifdef MMAP
   noPages = ((nu*sizeof(Header))-1)/getpagesize() + 1;
   cp = mmap(__endHeap, noPages*getpagesize(), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
   nu = (noPages*getpagesize())/sizeof(Header);
   __endHeap += noPages*getpagesize();
#else
   cp = sbrk(nu*sizeof(Header));
#endif
   if(cp == (void *) -1) {                                /* no space at all */
      perror("failed to get more memory");
      return NULL;
   }
   up = (Header *) cp;
   up->s.size = nu;
   free((void *)(up+1));
   return freep;
}

void * malloc(size_t nbytes) {
   Header *p, *prevp;
   Header * morecore(unsigned);
   unsigned nunits;

   if(nbytes == 0) return NULL;

   nunits = (nbytes+sizeof(Header)-1)/sizeof(Header) +1;

   if((prevp = freep) == NULL) {
      base.s.ptr = freep = prevp = &base;
      base.s.size = 0;
   }

#if STRATEGY == 1
   return first_fit(p, prevp, nunits);
#else
   return best_fit(p, prevp, nunits);
#endif // STRATEGY

}

void * realloc(void * ptr, size_t size) {

   /* In case that ptr is a null pointer,
    the function behaves like malloc */
   if (ptr == NULL) return malloc(size);

   /* Otherwise, if size is zero, the memory previously
   allocated at ptr is deallocated as if a call to free
   was made, and a null pointer is returned. */
   if(size == 0) {
      free(ptr);
      return NULL;
   }

   /* Allocate the needed space */
   void * newptr = malloc(size);

   /* If the function fails to allocate the requested
   block of memory, a null pointer is returned */
   if (newptr == NULL) return NULL;

   Header* oldHeaderPtr = ((Header*) ptr) -1;
   Header* newHeaderPtr = ((Header*) newptr) -1;

   /*print(oldHeaderPtr);*/
   /*print(newHeaderPtr);*/

   /* If we are shrinking the size */
   if((oldHeaderPtr->s.size) > (newHeaderPtr->s.size)) {
      /* Copy only up to the size of new */
      memcpy(newptr, ptr, (newHeaderPtr->s.size)*sizeof(Align));
   } else {
      /* Copy up to the size of old */
      memcpy(newptr, ptr, (oldHeaderPtr->s.size)*sizeof(Align));
   }

   free(ptr);
   return newptr;
}

void * first_fit(Header* p, Header* prevp, unsigned nunits) {

   for(p= prevp->s.ptr;  ; prevp = p, p = p->s.ptr) {
      if(p->s.size >= nunits) {                           /* big enough */
         if (p->s.size == nunits)                          /* exactly */
            prevp->s.ptr = p->s.ptr;
         else {                                            /* allocate tail end */
            p->s.size -= nunits;
            p += p->s.size;
            p->s.size = nunits;
         }
         freep = prevp;
         return (void *)(p+1);
      }
      if(p == freep)                                      /* wrapped around free list */
         if((p = morecore(nunits)) == NULL)
            return NULL;                                    /* none left */
   }

}

void * best_fit(Header* p, Header* prevp, unsigned nunits) {
   Header* best_ptr = NULL;

   /* loopen */
   for(p= prevp->s.ptr;  ; prevp = p, p = p->s.ptr) {

      /* if we got enough space */
      if(p->s.size >= nunits) {                           /* big enough */

         /* if it fits perfectly */
         if (p->s.size == nunits) {                         /* exactly */

            prevp->s.ptr = p->s.ptr;
            freep = prevp;
            return (void *)(p+1);

         }

         /* if we dont have a best ptr */
         if(best_ptr == NULL) {
            best_ptr = p;
         } else {
            if(p->s.size < best_ptr->s.size) {
               best_ptr = p;
            }
         }
      }

      if(best_ptr!=NULL) {
         p = best_ptr;
         p->s.size -= nunits;
         p += p->s.size;
         p->s.size = nunits;
         freep = prevp;
         return (void *)(p+1);
      }

      /* Nothing on the free list. Ask for more */
      if(p == freep)                                      /* wrapped around free list */
         if((p = morecore(nunits)) == NULL)
            return NULL;                                    /* none left */
   }

}

