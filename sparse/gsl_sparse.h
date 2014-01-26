/* gsl_sparse.h
 * 
 * Copyright (C) 2012-2014 Patrick Alken
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or (at
 * your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifndef __GSL_SPARSE_H__
#define __GSL_SPARSE_H__

#include <stdlib.h>

#include <gsl/gsl_math.h>
#include <gsl/gsl_vector.h>
#include <gsl/gsl_matrix.h>

#undef __BEGIN_DECLS
#undef __END_DECLS
#ifdef __cplusplus
# define __BEGIN_DECLS extern "C" {
# define __END_DECLS }
#else
# define __BEGIN_DECLS /* empty */
# define __END_DECLS /* empty */
#endif

__BEGIN_DECLS

/*
 * Triplet format:
 *
 * If data[n] = A_{ij}, then:
 *   i = A->i[n]
 *   j = A->p[n]
 *
 * Compressed column format:
 *
 * If data[n] = A_{ij}, then:
 *   i = A->i[n]
 *   A->p[j] <= n < A->p[j+1]
 * so that column j is stored in
 * [ data[p[j]], data[p[j] + 1], ..., data[p[j+1] - 1] ]
 */

typedef struct
{
  size_t size1; /* number of rows */
  size_t size2; /* number of columns */

  size_t *i;    /* row indices of size nzmax */
  double *data; /* matrix elements of size nzmax */

  /*
   * p contains the column indices (triplet) or column pointers (compcol)
   *
   * triplet:   p[n] = column number of element data[n]
   * comp. col: p[j] = index in data of first non-zero element in column j
   * comp. row: p[i] = index in data of first non-zero element in row i
   */
  size_t *p;

  size_t nzmax; /* maximum number of matrix elements */
  size_t nz;    /* number of non-zero values in matrix */

  size_t *work; /* workspace of size MAX(size1,size2) used in various routines */

  size_t flags;
} gsl_spmatrix;

#define GSL_SPMATRIX_TRIPLET      (1 << 0)
#define GSL_SPMATRIX_CCS          (1 << 1)

#define GSLSP_ISTRIPLET(m)        ((m)->flags & GSL_SPMATRIX_TRIPLET)
#define GSLSP_ISCCS(m)            ((m)->flags & GSL_SPMATRIX_CCS)

typedef struct
{
  size_t n;        /* size of linear system */
  size_t m;        /* dimension of Krylov subspace K_m */
  gsl_vector *r;   /* residual vector r = b - A*x */
  gsl_matrix *H;   /* Hessenberg matrix n-by-(m+1) */
  gsl_vector *tau; /* householder scalars */
  gsl_vector *y;   /* least squares rhs and solution vector */

  double *c;       /* Givens rotations */
  double *s;
} gsl_splinalg_gmres_workspace;

/*
 * Prototypes
 */

gsl_spmatrix *gsl_spmatrix_alloc(const size_t n1, const size_t n2);
gsl_spmatrix *gsl_spmatrix_alloc_nzmax(const size_t n1, const size_t n2,
                                       const size_t nzmax, const size_t flags);
void gsl_spmatrix_free(gsl_spmatrix *m);
int gsl_spmatrix_realloc(const size_t nzmax, gsl_spmatrix *m);
int gsl_spmatrix_set_zero(gsl_spmatrix *m);
size_t gsl_spmatrix_nnz(const gsl_spmatrix *m);

/* spcopy.c */
gsl_spmatrix *gsl_spmatrix_memcpy(const gsl_spmatrix *src);

/* spgetset.c */
double gsl_spmatrix_get(const gsl_spmatrix *m, const size_t i,
                        const size_t j);
int gsl_spmatrix_set(gsl_spmatrix *m, const size_t i, const size_t j,
                     const double x);

/* spcompress.c */
gsl_spmatrix *gsl_spmatrix_compress(const gsl_spmatrix *T);
void gsl_spmatrix_cumsum(const size_t n, size_t *c);

/* spoper.c */
int gsl_spmatrix_scale(gsl_spmatrix *m, const double x);
int gsl_spmatrix_minmax(const gsl_spmatrix *m, double *min_out,
                        double *max_out);
gsl_spmatrix *gsl_spmatrix_add(const gsl_spmatrix *a, const gsl_spmatrix *b);
int gsl_spmatrix_d2sp(gsl_spmatrix *S, const gsl_matrix *A);
int gsl_spmatrix_sp2d(gsl_matrix *A, const gsl_spmatrix *S);

/* spprop.c */
int gsl_spmatrix_equal(const gsl_spmatrix *a, const gsl_spmatrix *b);

/* spswap.c */
gsl_spmatrix *gsl_spmatrix_transpose_memcpy(const gsl_spmatrix *src);

/* spblas */
int gsl_spblas_dgemv(const double alpha, const gsl_spmatrix *A,
                     const gsl_vector *x, const double beta, gsl_vector *y);
gsl_spmatrix *gsl_spblas_dgemm(const double alpha, const gsl_spmatrix *A,
                               const gsl_spmatrix *B);
size_t gsl_spblas_scatter(const gsl_spmatrix *A, const size_t j, const double alpha,
                          size_t *w, double *x, const size_t mark, gsl_spmatrix *C,
                          size_t nz);

/* spgmres.c */
gsl_splinalg_gmres_workspace *gsl_splinalg_gmres_alloc(const size_t n);
void gsl_splinalg_gmres_free(gsl_splinalg_gmres_workspace *w);
int gsl_splinalg_gmres_solve(const gsl_spmatrix *A, const gsl_vector *b,
                             gsl_vector *x,
                             gsl_splinalg_gmres_workspace *w);
int gsl_splinalg_gmres_solve_x(const gsl_spmatrix *A,
                               const gsl_vector *b, const double tol,
                               gsl_vector *x,
                               gsl_splinalg_gmres_workspace *w);

__END_DECLS

#endif /* __GSL_SPARSE_H__ */
