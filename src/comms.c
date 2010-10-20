/*
 * This file provides the layer on top of GASNet, ARMCI or whatever.
 * API should be formalized at some point, but basically everything
 * that starts with "__comms_"
 */

#include <ctype.h>

#include "gasnet_safe.h"

#include "state.h"
#include "memalloc.h"
#include "warn.h"
#include "dispatch.h"
#include "atomic.h"
#include "comms.h"

#include "shmem.h"

/*
 * define accepted size units.  Table set up to favor expected units
 */

struct unit_lookup {
  char unit;
  size_t size;
};

static struct unit_lookup units[] =
  {
    { 'g', 1024L * 1024L * 1024L         },
    { 'm', 1024L * 1024L                 },
    { 'k', 1024L                         },
    { 't', 1024L * 1024L * 1024L * 1024L }
  };
static const int nunits = sizeof(units) / sizeof(units[0]);

static size_t
__comms_get_segment_size(void)
{
  char unit = '\0';
  size_t mul = 1;
  char *p;
  char *mlss_str = __comms_getenv("SHMEM_SYMMETRIC_HEAP_SIZE");

  if (mlss_str == (char *) NULL) {
    return (size_t) gasnet_getMaxLocalSegmentSize();
  }

  p = mlss_str;
  while (*p != '\0') {
    if (! isdigit(*p)) {
      unit = *p;
      *p = '\0';		/* get unit, chop */
      break;
    }
    p += 1;
  }

  if (unit != '\0') {
    int i;
    int foundit = 0;
    struct unit_lookup *up = (struct unit_lookup *) units;

    unit = tolower(unit);
    for (i = 0; i < nunits; up += 1, i += 1) {
      if (up->unit == unit) {	/* walk the table, assign if unit matches */
	mul = up->size;
	foundit = 1;
	break;
      }
    }
    if (! foundit) {
      __shmem_warn(SHMEM_LOG_FATAL,
		   "unknown data size unit \"%c\" in symmetric heap specification",
		   unit);
    }
  }

  return mul * (size_t) strtol(mlss_str, (char **) NULL, 10);
}

/*
 * start of handlers
 */

#define GASNET_HANDLER_SWAP_OUT 128
#define GASNET_HANDLER_SWAP_BAK 129

static gasnet_handlerentry_t handlers[] =
  {
    { GASNET_HANDLER_SWAP_OUT, handler_swap_out },
    { GASNET_HANDLER_SWAP_BAK, handler_swap_bak }
  };
static const int nhandlers = sizeof(handlers) / sizeof(handlers[0]);

/*
 * end of handlers
 */

void
__comms_init(void)
{
  /*
   * fake the command-line args
   */
  int argc = 1;
  char **argv;

  argv = (char **) malloc(argc * sizeof(*argv));
  if (argv == (char **) NULL) {
    __shmem_warn(SHMEM_LOG_FATAL,
                 "could not allocate memory for GASNet initialization");
  }
  argv[0] = "shmem";
  
  GASNET_SAFE(
	      gasnet_init(&argc, &argv)
	      );

  __state.mype = __comms_mynode();
  __state.numpes = __comms_nodes();
  __state.heapsize = __comms_get_segment_size();

  GASNET_SAFE(
	      gasnet_attach(handlers, nhandlers,
			    __state.heapsize,
			    0)
	      );

  __shmem_warn(SHMEM_LOG_DEBUG,
	       "symmetric heap size is %ld bytes",
	       __state.heapsize
	       );

  __comms_set_waitmode(SHMEM_COMMS_SPINBLOCK);

  __comms_barrier_all();
}

/*
 * allow the runtime to change the spin/block behavior dynamically,
 * would allow adaptivity
 */
void
__comms_set_waitmode(int mode)
{
  int gm;
  const char *mstr;

  switch (mode) {
  case SHMEM_COMMS_SPINBLOCK:
    gm = GASNET_WAIT_SPINBLOCK;
    mstr = "spinblock";
    break;
  case SHMEM_COMMS_SPIN:
    gm = GASNET_WAIT_SPIN;
    mstr = "spin";
    break;
  case SHMEM_COMMS_BLOCK:
    gm = GASNET_WAIT_BLOCK;
    mstr = "block";
    break;
  default:
    __shmem_warn(SHMEM_LOG_FATAL,
		 "tried to set unknown wait mode %d", mode);
    /* NOT REACHED */
    break;
  }

  GASNET_SAFE( gasnet_set_waitmode(gm) );

  __shmem_warn(SHMEM_LOG_DEBUG,
	       "set waitmode to %s",
	       mstr);
}

void
__comms_poll(void)
{
  GASNET_BEGIN_FUNCTION();
  gasnet_AMPoll();
}

void
__comms_shutdown(int status)
{
  __comms_barrier_all();
  
  gasnet_exit(status);
}

char *
__comms_getenv(const char *name)
{
  return gasnet_getenv(name);
}

int
__comms_mynode(void)
{
  return (int) gasnet_mynode();
}

int
__comms_nodes(void)
{
  return (int) gasnet_nodes();
}

void
__comms_put(void *dst, void *src, size_t len, int pe)
{
  gasnet_put_nbi(pe, dst, src, len);
}

void
__comms_get(void *dst, void *src, size_t len, int pe)
{
  gasnet_get(dst, pe, src, len);
}

/*
 * not completely sure about using longs in these two:
 * it's big enough and hides the gasnet type: is that good enough?
 */

void
__comms_put_val(void *dst, long src, size_t len, int pe)
{
  gasnet_put_nbi_val(pe, dst, src, len);
}

long
__comms_get_val(void *src, size_t len, int pe)
{
  return gasnet_get_val(pe, src, len);
}

#define COMMS_TYPE_PUT_NB(Name, Type)					\
  void *								\
  __comms_##Name##_put_nb(Type *target, Type *source, size_t len, int pe) \
  {									\
    return gasnet_put_nb(pe, target, source, sizeof(Type) * len);	\
  }

COMMS_TYPE_PUT_NB(short, short)
COMMS_TYPE_PUT_NB(int, int)
COMMS_TYPE_PUT_NB(long, long)
COMMS_TYPE_PUT_NB(longdouble, long double)
COMMS_TYPE_PUT_NB(longlong, long long)
COMMS_TYPE_PUT_NB(double, double)
COMMS_TYPE_PUT_NB(float, float)

_Pragma("weak __comms_putmem_nb=__comms_long_put_nb")

void
__comms_wait_nb(void *h)
{
  gasnet_wait_syncnb((gasnet_handle_t ) h);
}

void
__comms_fence(void)
{
  gasnet_wait_syncnbi_all();
}



#if 1

static int barcount = 0;
static int barflag = 0; // GASNET_BARRIERFLAG_ANONYMOUS;

void
__comms_barrier_all(void)
{
  // GASNET_BEGIN_FUNCTION();

  /* wait for gasnet to finish pending puts/gets */
  gasnet_wait_syncnbi_all();

  /* use gasnet's global barrier */
  gasnet_barrier_notify(barcount, barflag);
  GASNET_SAFE( gasnet_barrier_wait(barcount, barflag) );

  // barcount = 1 - barcount;
  barcount += 1;
}

#else

void
__comms_barrrier_all(void)
{
  __comms_barrier(0, 0, __state.numpes, syncit);
}

#endif



void
__comms_barrier(int PE_start, int logPE_stride, int PE_size, long *pSync)
{
  GASNET_BEGIN_FUNCTION();

  __comms_fence();

  if (__state.mype == PE_start) {
    int step = 1 << logPE_stride;
    int i;
    int thispe = PE_start;
    for (i = 1; i < PE_size; i+= 1) {
      thispe += step;
      shmem_wait(& pSync[thispe], _SHMEM_SYNC_VALUE);
      pSync[thispe] = _SHMEM_SYNC_VALUE;
      shmem_long_p(& pSync[thispe], _SHMEM_SYNC_VALUE, thispe);
    }
  }
  else {
    shmem_long_p(& pSync[__state.mype], _SHMEM_SYNC_VALUE + 1, PE_start);
    shmem_wait(& pSync[__state.mype], _SHMEM_SYNC_VALUE + 1);
  }

  __comms_fence();

}

/*
 * initialize the symmetric segments
 */

static gasnet_seginfo_t *seginfo_table;

/* UNUSED */
#define ROUNDUP(v, n) (((v) + (n)) & ~((n) - 1))

void
__symmetric_memory_init(void)
{
  /*
   * calloc zeroes for us
   */
  seginfo_table = (gasnet_seginfo_t *)calloc(__state.numpes,
                                             sizeof(gasnet_seginfo_t));
  if (seginfo_table == (gasnet_seginfo_t *) NULL) {
    __shmem_warn(SHMEM_LOG_FATAL,
                 "could not allocate GASNet segments");
  }

  /*
   * prep the segments for use across all PEs
   */
  GASNET_SAFE( gasnet_getSegmentInfo(seginfo_table, __state.numpes) );

  /*
   * each PE manages its own segment, but can see addresses from all PEs
   */
  __mem_init(seginfo_table[__state.mype].addr,
	     seginfo_table[__state.mype].size);

  __comms_barrier_all();
}

void
__symmetric_memory_finalize(void)
{
  __mem_finalize();
}

/*
 * where the symmetric memory starts on the given PE
 */
void *
__symmetric_var_base(int pe)
{
  return seginfo_table[pe].addr;
}

/*
 * is the address in the managed symmetric area?
 */
 int
__symmetric_var_in_range(void *addr, int pe)
{
  void *top = seginfo_table[pe].addr + seginfo_table[pe].size;
  return (seginfo_table[pe].addr <= addr) && (addr <= top) ? 1 : 0;
}

/*
 * translate my "dest" to corresponding address on PE "pe"
 */
void *
__symmetric_var_offset(void *dest, int pe)
{
  size_t offset = (char *)dest - (char *)__symmetric_var_base(__state.mype);
  char *rdest = (char *)__symmetric_var_base(pe) + offset;
  return (void *)rdest;
}

/*
 * -- swap handlers --
 */
gasnet_hsl_t swap_out_lock = GASNET_HSL_INITIALIZER;
gasnet_hsl_t swap_bak_lock = GASNET_HSL_INITIALIZER;

typedef struct {
  void *s_symm_addr;		/* sender symmetric var */
  void *r_symm_addr;		/* recipient symmetric car */
  long value;			/* value to be swapped */
  int sentinel;			/* end of transaction marker */
  int *sentinel_addr;		/* addr of marker for copied payload */
} swap_payload_t;

/*
 * called by remote PE to do the swap.  Store new value, send back old value
 */
void
handler_swap_out(gasnet_token_t token,
                 void *buf, size_t bufsiz,
                 gasnet_handlerarg_t unused)
{
  long old;
  swap_payload_t *pp = (swap_payload_t *) buf;

  gasnet_hsl_lock(& swap_out_lock);

  old = *(long *) pp->r_symm_addr;
  *(long *) pp->r_symm_addr = pp->value;
  pp->value = old;

  gasnet_hsl_unlock(& swap_out_lock);

  /* return the updated payload */
  gasnet_AMReplyMedium1(token, GASNET_HANDLER_SWAP_BAK, buf, bufsiz, unused);
}

/*
 * called by swap invoker when old value returned by remote PE
 */
void
handler_swap_bak(gasnet_token_t token,
                 void *buf, size_t bufsiz,
                 gasnet_handlerarg_t unused)
{
  swap_payload_t *pp = (swap_payload_t *) buf;

  gasnet_hsl_lock(& swap_bak_lock);

  *(long *) pp->s_symm_addr = pp->value;

  *(pp->sentinel_addr) = 1;

  gasnet_hsl_unlock(& swap_bak_lock);
}


long
__comms_request(void *target, long value, int pe)
{
  long retval;
  // allocate p, TODO: check result
  swap_payload_t *p = (swap_payload_t *) malloc(sizeof(*p));
  p->s_symm_addr = target;
  p->r_symm_addr = __symmetric_var_offset(target, pe);
  p->value = value;
  p->sentinel = 0;
  p->sentinel_addr = &(p->sentinel); /* this one, not the copy */

  gasnet_AMRequestMedium1(pe, GASNET_HANDLER_SWAP_OUT,
			  p, sizeof(*p),
			  0);

  GASNET_BLOCKUNTIL(p->sentinel);

  retval = p->value;

  free(p);

  return retval;
}
