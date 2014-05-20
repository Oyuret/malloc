/**
* NAME:
* malloc - a custom implementation of malloc, free and realloc
*
* DESCRIPTION:
* This is a simple custom implementation of malloc, free and realloc as 
* found in the default C libraries. Functions that are found in the library 
* version of malloc.h may not work.
*
* EXAMPLES:
* void * pointer = malloc(sizeof(a));
* pointer = realloc(pointer, sizeof(b));
* free(pointer);
*
* AUTHOR:
* Johan Storby, Yuri Stange, 2014
*
* SEE ALSO:
* malloc(3), realloc(3), free(1)
*
*/

#include "brk.h"
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/mman.h>

#define NALLOC 1024 /* minimum #units to request */

#ifndef STRATEGY /* Strategy isn't defined*/
#define STRATEGY 1 /* Go for strategy 1 (first fit)*/
#endif /* STRATEGY */

typedef long Align; /* for alignment to long boundary */

union header { /* block header */
  struct {
    union header *ptr; /* next block if on free list */
    unsigned size; /* size of this block - what unit? */
  } s;
  Align x; /* force alignment of blocks */
};

typedef union header Header;

static Header base; /* empty list to get started */
static Header *freep = NULL; /* start of free list */


/* free: put block ap in the free list */
/* This is the code provided in the example code, did not need any changes. */
void free(void * ap) {
  Header *bp, *p;

  if(ap == NULL) return; /* Nothing to do */

  bp = (Header *) ap - 1; /* point to block header */
  for(p = freep; !(bp > p && bp < p->s.ptr); p = p->s.ptr)
    if(p >= p->s.ptr && (bp > p || bp < p->s.ptr))
      break; /* freed block at start or end of arena, so we don't need to join other blocks */

  if(bp + bp->s.size == p->s.ptr) { /* join to upper nb */
    bp->s.size += p->s.ptr->s.size; /* Increase the before-pointer with the size of p and join them */
    bp->s.ptr = p->s.ptr->s.ptr;
  } else
    bp->s.ptr = p->s.ptr;
  if(p + p->s.size == bp) { /* join to lower nbr */
    p->s.size += bp->s.size; /* Increase p with the size of the the before-pointers and join them */
    p->s.ptr = bp->s.ptr;
  } else
    p->s.ptr = bp;
  freep = p;   /*Set the free pointer to our local p */
}



#ifdef MMAP
/* If mmap is defined we can use endHeap to find the end of the heap. */
static void * __endHeap = 0;

void * endHeap(void) {
  if(__endHeap == 0) __endHeap = sbrk(0);
  return __endHeap;
}
#endif


/* morecore: ask system for more memory */
/* Using the provided example code, we have not made any changes. */
static Header *morecore(unsigned nu) {
  void *cp;
  Header *up;
#ifdef MMAP
  unsigned noPages;
  if(__endHeap == 0) __endHeap = sbrk(0);
#endif

  if(nu < NALLOC) /* We always ask for at least NALLOC amount at a time */
    nu = NALLOC;

 /* Get more memory! */
#ifdef MMAP
  noPages = ((nu*sizeof(Header))-1)/getpagesize() + 1;
  cp = mmap(__endHeap, noPages*getpagesize(), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  nu = (noPages*getpagesize())/sizeof(Header);
  __endHeap += noPages*getpagesize();
#else
  cp = sbrk(nu*sizeof(Header));
#endif
  if(cp == (void *) -1) { /* No more memory :( */
    perror("failed to get more memory");
    return NULL;
  }
  up = (Header *) cp;
  up->s.size = nu;
  free((void *)(up+1));
  return freep; /* Return the start of the free area we now have. */
}

/* Malloc! Allocate memory. */
void * malloc(size_t nbytes) {
  Header *p, *prevp; /* Pointer, and previous pointer */
  Header * morecore(unsigned); /* Function pointer to morecore */
  unsigned nunits;

  if(nbytes == 0) return NULL; /* Request 0 bytes? Return null. */

  /* Get the amount of units we need */
  nunits = (nbytes+sizeof(Header)-1)/sizeof(Header) +1;
  
  /* If this is the first run and we have no base set, then set a base. */
  if((prevp = freep) == NULL) {
    base.s.ptr = freep = prevp = &base;
    base.s.size = 0;
  }

#if STRATEGY == 1 /* first fit */
  for(p= prevp->s.ptr; ; prevp = p, p = p->s.ptr) { /* Loop through our free list */
    if(p->s.size >= nunits) { /* Big enough? */
      if (p->s.size == nunits) /* Fits exactly, set our pointer here */
        prevp->s.ptr = p->s.ptr;
      else { /* Fits, but not exactly, allocate as much as we need, and leave the rest still avaialble. */
        p->s.size -= nunits;
        p += p->s.size;
        p->s.size = nunits;
      }
      freep = prevp; /* Set the new start of the free pointer */
      return (void *)(p+1); /* Return the pointer */
    }
    if(p == freep) /* Couldn't find any space in our exists free list, ask for more memory. */
      if((p = morecore(nunits)) == NULL)
        return NULL; /* We couldn't get more memory, so return null. */
  }
#else /* best fit */

  /* Holders for the best fitting in list and it's previous */
  Header* best_ptr = NULL;
  Header* best_ptr_prev = NULL;

  /* Loop though the free list */
  for(p= prevp->s.ptr; ; prevp = p, p = p->s.ptr) {

    /* If it fits */
    if(p->s.size >= nunits) {

      /* if it fits perfectly */
      if (p->s.size == nunits) { 
       /* We won't find anything better than a prefect fit, so use this right  away */

        prevp->s.ptr = p->s.ptr; /* Set the previous pointer to point at the current. */
        freep = prevp;  /* Freep starts at prevp now. */
        return (void *)(p+1); /* Return the pointer */

      }

      /* It fits, but not perfectly. */

      if(best_ptr == NULL) { 
       /* If we have no best pointer yet, set this as the best pointer. */

        best_ptr = p;
        best_ptr_prev = prevp;
      } else {
        /* We already have a best pointer, but is the new one better? */
        if(p->s.size < best_ptr->s.size) {
          /* It is better! So make it the new best pointer. */
          best_ptr = p;
          best_ptr_prev = prevp;
        }
      }
    }

    /* We traversed the whole free list and found nothing perfect */
    if(p == freep) {

	/* Do we have a best pointer? If so, use it! */
	  if(best_ptr!=NULL) {

	    /* Set it so the unused space at the tail end can be used by something else. */
	    best_ptr->s.size -= nunits;
	    best_ptr += best_ptr->s.size;
	    best_ptr->s.size = nunits;
	    freep = best_ptr_prev; /* Set freep to the previous pointer of the best pointer. */
	    return (void *)(best_ptr+1); /* Return the best pointer */

	  } else { /* We have no best pointer, ask morecore for more memory */
		if ((p = morecore(nunits)) == NULL) {
                	return NULL; /* Out of memory */
            }
          }
    }

  }

 
   /* This should never be reached. If we for any reason break out of the loop, 
   something went wrong, so return null */
  return NULL; 

#endif /* STRATEGY */

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

  /* Since the ptr given was previously allocated with our malloc we can
adress is as our base Header */
  Header* oldHeaderPtr = ((Header*) ptr) -1;
  Header* newHeaderPtr = ((Header*) newptr) -1;

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
