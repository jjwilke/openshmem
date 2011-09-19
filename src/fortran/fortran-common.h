/* (c) 2011 University of Houston System.  All rights reserved. */


#ifndef FORTRAN_COMMON_H
#define FORTRAN_COMMON_H 1

#if defined(FORTRAN_SINGLE_UNDERSCORE)

#define FORTRANIFY(sym)    sym##_

#elif defined(FORTRAN_DOUBLE_UNDERSCORE)

#define FORTRANIFY(sym)    sym##__

#else /* no idea */

#define FORTRANIFY(sym) sym

#endif /* FORTRAN_SINGLE_UNDERSCORE */

#define FORTRANIFY_VOID_VOID(F) \
  void inline FORTRANIFY(F) (void) { F(); }

#endif /* FORTRAN_COMMON_H */
