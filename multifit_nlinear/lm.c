/* multifit_nlinear/lm.c
 * 
 * Copyright (C) 2014, 2015 Patrick Alken
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

#include <config.h>
#include <gsl/gsl_math.h>
#include <gsl/gsl_vector.h>
#include <gsl/gsl_matrix.h>
#include <gsl/gsl_linalg.h>
#include <gsl/gsl_multifit_nlinear.h>
#include <gsl/gsl_errno.h>
#include <gsl/gsl_blas.h>

#define SCALE 0

/*
 * This module contains an implementation of the Levenberg-Marquardt
 * algorithm for nonlinear optimization problems. This implementation
 * closely follows the following works:
 *
 * [1] H. B. Nielsen, K. Madsen, Introduction to Optimization and
 *     Data Fitting, Informatics and Mathematical Modeling,
 *     Technical University of Denmark (DTU), 2010.
 */

typedef struct
{
  gsl_matrix *A;             /* J^T J */
  gsl_matrix *work_JTJ;      /* copy of J^T J */
  gsl_vector *diag;          /* D = diag(J^T J) */
  gsl_vector *rhs;           /* rhs vector = -g = -J^T f */
  gsl_vector *x_trial;       /* trial parameter vector */
  gsl_vector *f_trial;       /* trial function vector */
  gsl_vector *work;          /* workspace length p */
  long nu;                   /* nu */
  double mu;                 /* LM damping parameter mu */
  double tau;                /* initial scale factor for mu */
} lm_state_t;

#include "lmmisc.c"

#define LM_ONE_THIRD         (0.333333333333333)

static int lm_alloc (void *vstate, const size_t n, const size_t p);
static void lm_free(void *vstate);
static int lm_set(void *vstate, const gsl_vector * swts,
                      gsl_multifit_nlinear_fdf *fdf,
                      gsl_vector *x, gsl_vector *f, gsl_matrix *J);
static int lm_iterate(void *vstate, const gsl_vector *swts,
                          gsl_multifit_nlinear_fdf *fdf,
                          gsl_vector *x, gsl_vector *f, gsl_matrix *J,
                          gsl_vector *dx);

static int
lm_alloc (void *vstate, const size_t n, const size_t p)
{
  lm_state_t *state = (lm_state_t *) vstate;

  state->A = gsl_matrix_alloc(p, p);
  if (state->A == NULL)
    {
      GSL_ERROR ("failed to allocate space for A", GSL_ENOMEM);
    }

  state->diag = gsl_vector_alloc(p);
  if (state->diag == NULL)
    {
      GSL_ERROR ("failed to allocate space for diag", GSL_ENOMEM);
    }

  state->rhs = gsl_vector_alloc(p);
  if (state->rhs == NULL)
    {
      GSL_ERROR ("failed to allocate space for rhs", GSL_ENOMEM);
    }

  state->work = gsl_vector_alloc(p);
  if (state->work == NULL)
    {
      GSL_ERROR ("failed to allocate space for work", GSL_ENOMEM);
    }

  state->work_JTJ = gsl_matrix_alloc(p, p);
  if (state->work_JTJ == NULL)
    {
      GSL_ERROR ("failed to allocate space for JTJ workspace", GSL_ENOMEM);
    }

  state->x_trial = gsl_vector_alloc(p);
  if (state->x_trial == NULL)
    {
      GSL_ERROR ("failed to allocate space for x_trial", GSL_ENOMEM);
    }

  state->f_trial = gsl_vector_alloc(n);
  if (state->f_trial == NULL)
    {
      GSL_ERROR ("failed to allocate space for f_trial", GSL_ENOMEM);
    }

  state->tau = 1.0e-3;

  return GSL_SUCCESS;
} /* lm_alloc() */

static void
lm_free(void *vstate)
{
  lm_state_t *state = (lm_state_t *) vstate;

  if (state->A)
    gsl_matrix_free(state->A);

  if (state->diag)
    gsl_vector_free(state->diag);

  if (state->rhs)
    gsl_vector_free(state->rhs);

  if (state->work)
    gsl_vector_free(state->work);

  if (state->work_JTJ)
    gsl_matrix_free(state->work_JTJ);

  if (state->x_trial)
    gsl_vector_free(state->x_trial);

  if (state->f_trial)
    gsl_vector_free(state->f_trial);
} /* lm_free() */

static int
lm_set(void *vstate, const gsl_vector *swts,
       gsl_multifit_nlinear_fdf *fdf, gsl_vector *x,
       gsl_vector *f, gsl_matrix *J)
{
  int status;
  lm_state_t *state = (lm_state_t *) vstate;
  const size_t p = x->size;
  size_t i;

  /* initialize counters for function and Jacobian evaluations */
  fdf->nevalf = 0;
  fdf->nevaldf = 0;

  /* evaluate function and Jacobian at x and apply weight transform */
  status = gsl_multifit_nlinear_eval_f(fdf, x, swts, f);
  if (status)
   return status;

  status = gsl_multifit_nlinear_eval_df(fdf, x, f, swts, J);
  if (status)
    return status;

  /* compute rhs = -J^T f */
  gsl_blas_dgemv(CblasTrans, -1.0, J, f, 0.0, state->rhs);

#if SCALE
  gsl_vector_set_zero(state->diag);
#else
  gsl_vector_set_all(state->diag, 1.0);
#endif

  /* set default parameters */
  state->nu = 2;

#if SCALE
  state->mu = state->tau;
#else
  /* compute mu_0 = tau * max(diag(J^T J)) */
  state->mu = -1.0;
  for (i = 0; i < p; ++i)
    {
      gsl_vector_view c = gsl_matrix_column(J, i);
      double result; /* (J^T J)_{ii} */

      gsl_blas_ddot(&c.vector, &c.vector, &result);
      state->mu = GSL_MAX(state->mu, result);
    }

  state->mu *= state->tau;
#endif

  return GSL_SUCCESS;
} /* lm_set() */

/*
lm_iterate()
  This function performs 1 iteration of the LM algorithm 6.18
from [1]. The algorithm is slightly modified to loop until we
find an acceptable step dx, in order to guarantee that each
function call contains a new input vector x.

Args: vstate - lm workspace
      swts   - data weights (NULL if unweighted)
      fdf    - function and Jacobian pointers
      x      - on input, current parameter vector
               on output, new parameter vector x + dx
      f      - on input, f(x)
               on output, f(x + dx)
      J      - on input, J(x)
               on output, J(x + dx)
      dx     - (output only) parameter step vector

Notes:
1) On input, the following must be initialized in state:
nu, mu, rhs

2) On output, the following are updated with the current iterates:
nu, mu, rhs

rhs needs to be set on each output, so that lm_gradient supplies
the correct g = J^T f
*/

static int
lm_iterate(void *vstate, const gsl_vector *swts,
               gsl_multifit_nlinear_fdf *fdf, gsl_vector *x,
               gsl_vector *f, gsl_matrix *J, gsl_vector *dx)
{
  int status;
  lm_state_t *state = (lm_state_t *) vstate;
  gsl_matrix *A = state->A;                   /* J^T J */
  gsl_vector *rhs = state->rhs;               /* -g = -J^T f */
  gsl_vector *x_trial = state->x_trial;       /* trial x + dx */
  gsl_vector *f_trial = state->f_trial;       /* trial f(x + dx) */
  gsl_vector *diag = state->diag;             /* diag(D) */
  double dF;                                  /* F(x) - F(x + dx) */
  double dL;                                  /* L(0) - L(dx) */
  int foundstep = 0;                          /* found step dx */

  /* compute A = J^T J */
  status = gsl_blas_dsyrk(CblasLower, CblasTrans, 1.0, J, 0.0, A);
  if (status)
    return status;

  /* copy lower triangle to upper */
  gsl_matrix_transpose_tricpy('L', 0, A, A);

#if SCALE
  lm_update_diag(J, diag);
#endif

  /* loop until we find an acceptable step dx */
  while (!foundstep)
    {
      /* solve (A + mu*I) dx = g */
      status = lm_calc_dx(state->mu, A, rhs, dx, state);
      if (status)
        return status;

      /* compute x_trial = x + dx */
      lm_trial_step(x, dx, x_trial);

      /* compute f(x + dx) */
      status = gsl_multifit_nlinear_eval_f(fdf, x_trial, swts, f_trial);
      if (status)
       return status;

      /* compute dF = F(x) - F(x + dx) */
      dF = lm_calc_dF(f, f_trial);

      /* compute dL = L(0) - L(dx) = dx^T (mu*dx - g) */
      dL = lm_calc_dL(state->mu, diag, dx, rhs);

      /* check that rho = dF/dL > 0 */
      if ((dL > 0.0) && (dF >= 0.0))
        {
          /* reduction in error, step acceptable */

          double tmp;

          /* update LM parameter mu */
          tmp = 2.0 * (dF / dL) - 1.0;
          tmp = 1.0 - tmp*tmp*tmp;
          state->mu *= GSL_MAX(LM_ONE_THIRD, tmp);
          state->nu = 2;

          /* compute J <- J(x + dx) */
          status = gsl_multifit_nlinear_eval_df(fdf, x_trial, f_trial,
                                                swts, J);
          if (status)
            return status;

          /* update x <- x + dx */
          gsl_vector_memcpy(x, x_trial);

          /* update f <- f(x + dx) */
          gsl_vector_memcpy(f, f_trial);

          /* compute new rhs = -J^T f */
          gsl_blas_dgemv(CblasTrans, -1.0, J, f, 0.0, rhs);

          foundstep = 1;
        }
      else
        {
          long nu2;

          /* step did not reduce error, reject step */
          state->mu *= (double) state->nu;
          nu2 = state->nu << 1; /* 2*nu */
          if (nu2 <= state->nu)
            {
              gsl_vector_view d = gsl_matrix_diagonal(A);

              /*
               * nu has wrapped around / overflown, reset mu and nu
               * to original values and break to force another iteration
               */
              /*GSL_ERROR("nu parameter has overflown", GSL_EOVRFLW);*/
              state->nu = 2;
              state->mu = state->tau * gsl_vector_max(&d.vector);
              break;
            }
          state->nu = nu2;
        }
    } /* while (!foundstep) */

  return GSL_SUCCESS;
} /* lm_iterate() */

static int
lm_gradient(void *vstate, gsl_vector * g)
{
  lm_state_t *state = (lm_state_t *) vstate;
  gsl_vector_memcpy(g, state->rhs);
  gsl_vector_scale(g, -1.0);
  return GSL_SUCCESS;
}

static const gsl_multifit_nlinear_type lm_type =
{
  "lm",
  sizeof(lm_state_t),
  &lm_alloc,
  &lm_set,
  &lm_iterate,
  &lm_gradient,
  &lm_free
};

const gsl_multifit_nlinear_type *gsl_multifit_nlinear_lm = &lm_type;
