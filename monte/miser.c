/* monte/miser.c
 * 
 * Copyright (C) 1996, 1997, 1998, 1999, 2000 Michael Booth
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* MISER.  Based on the algorithm described in Numerical recipes. */

/* Author: MJB */
/* RCS: $Id$ */

#include <config.h>
#include <math.h>
#include <stdlib.h>

#include <gsl/gsl_math.h>
#include <gsl/gsl_errno.h>
#include <gsl/gsl_rng.h>
#include <gsl/gsl_monte.h>
#include <gsl/gsl_monte_miser.h>

int
gsl_monte_miser_integrate (gsl_monte_function * f,
			   const double xl[], const double xu[],
			   size_t dim, size_t calls,
			   gsl_rng * r,
			   gsl_monte_miser_state * state,
			   double *result, double *abserr)
{
  int status = 0;

  size_t n, estimate_calls, calls_l, calls_r;
  size_t i;
  size_t i_bisect;
  int found_best;

  double res_est = 0, err_est = 0;
  double res_r = 0, err_r = 0, res_l = 0, err_l = 0;
  double fval;
  double xbi_l, xbi_m, xbi_r, s;

  double vol;
  double weight_l, weight_r;

  double *xmid = state->xmid;
  double *sigma_l = state->sigma_l, *sigma_r = state->sigma_r;

  if (dim > state->dim)
    {
      GSL_ERROR ("number of dimensions exceeds allocated size", GSL_EINVAL);
    }

  for (i = 0; i < dim; i++)
    {
      if (xu[i] <= xl[i])
	{
	  GSL_ERROR ("xu must be greater than xl", GSL_EINVAL);
	}

      if (xu[i] - xl[i] > GSL_DBL_MAX)
	{
	  GSL_ERROR ("Range of integration is too large, please rescale",
		     GSL_EINVAL);
	}
    }

  if (state->alpha < 0)
    {
      GSL_ERROR ("alpha must be non-negative", GSL_EINVAL);
    }

  if (calls < state->min_calls_per_bisection)
    {
      status = gsl_monte_plain_integrate (f, xl, xu, dim, calls,
                                          r, state->plain_state,
                                          result, abserr);
      return status;
    }

  /* FIXME: This is bad when the estimate_calls come out less than
     or near to dim, because then we will get subplanes.  This
     is also an issue for min_calls.  */

  estimate_calls = GSL_MAX (state->min_calls, calls * (state->estimate_frac));

  if (estimate_calls <= dim)
    {
      GSL_ERROR ("estimate calls is close to dim!", GSL_ESANITY);
    }

  vol = 1;

  for (i = 0; i < dim; i++)
    {
      /* Flip a coin to bisect the integration region with some fuzz */
      s = (gsl_rng_uniform (r) - 0.5) >= 0.0 ? state->dither : -state->dither;
      state->xmid[i] = (0.5 + s) * xl[i] + (0.5 - s) * xu[i];

      /* Compute volume */
      vol *= xu[i] - xl[i];
    }

  /* The idea is to chose the direction to bisect based on which will
     give the smallest total variance.  We could (and may do so later)
     use MC to compute these variances.  But the NR guys simply estimate
     the variances by finding the min and max function values 
     for each half-region for each bisection. */

  estimate_corrmc (f, xl, xu, dim, estimate_calls, 
                   r, state, 
                   &res_est, &err_est,
                   xmid, sigma_l, sigma_r);

  /* Now find direction with the smallest total "variance" */

  {
    double best_var = GSL_DBL_MAX;
    double beta = 2.0 / (1.0 + state->alpha);
    found_best = 0;
    i_bisect = 0;
    weight_l = weight_r = 1.0;
    
    for (i = 0; i < dim; i++)
      {
        if (sigma_l[i] >= 0 && sigma_r[i] >= 0)
          {
            /* estimates are okay */
            double var = pow (sigma_l[i], beta) + pow (sigma_r[i], beta);
            
            if (var <= best_var)
              {
                found_best = 1;
                best_var = var;
                i_bisect = i;
                weight_l = pow (sigma_l[i], beta);
                weight_r = pow (sigma_r[i], beta);
              }
          }
        else
          {
            if ((sigma_l[i] < 0) && (state->verbose > 0))
              {
                /* FIXME: Get a proper error code here */
                GSL_ERROR ("no points in left-half space!", GSL_ESANITY);
              }
            if ((sigma_r[i] < 0) && (state->verbose > 0))
              {
                /* FIXME: Get a proper error code here */
                GSL_ERROR ("no points in right-half space!", GSL_ESANITY);
              }
          }
      }
  }

  if (! found_best)
    {
      /* All were same, so chose direction at random */

      i_bisect = gsl_rng_uniform_int (r, dim);
    }

  xbi_l = xl[i_bisect];
  xbi_m = xmid[i_bisect];
  xbi_r = xu[i_bisect];

  /* Get the actual fractional sizes of the two "halves", and
     distribute the remaining calls among them */

  {
    double fraction_l = fabs ((xbi_m - xbi_l) / (xbi_r - xbi_l));
    double fraction_r = 1 - fraction_l ;

    double a = fraction_l * weight_l;
    double b = fraction_r * weight_r;

    calls_l = calls * a / (a + b);
    calls_r = calls * b / (a + b);
  }

  /* Compute the integral for the left hand side of the bisection */

  /* Due to the recursive nature of the algorithm we must allocate
     some new memory for each recursive call */

  {
    int status;

    double * xu_tmp = (double *) malloc (dim * sizeof(double));

    if (xu_tmp == 0)
      {
        GSL_ERROR_VAL ("out of memory for left workspace", GSL_ENOMEM, 0);
      }

    for (i = 0; i < dim; i++)
      {
        xu_tmp[i] = xu[i];
      }
    
    xu_tmp[i_bisect] = xbi_m;
    
    status = gsl_monte_miser_integrate (f, xl, xu_tmp,
                                        dim, calls_l, r, state, 
                                        &res_l, &err_l);
    free (xu_tmp);

    if (status != GSL_SUCCESS)
      {
        return status ;
      }
  }

  /* Compute the integral for the right hand side of the bisection */

  {
    int status;

    double * xl_tmp = (double *) malloc (dim * sizeof(double));

    if (xl_tmp == 0)
      {
        GSL_ERROR_VAL ("out of memory for right workspace", GSL_ENOMEM, 0);
      }

    for (i = 0; i < dim; i++)
      {
        xl_tmp[i] = xl[i];
      }
    
    xl_tmp[i_bisect] = xbi_m;
    
    status = gsl_monte_miser_integrate (f, xl_tmp, xu,
                                        dim, calls_r, r, state, 
                                        &res_r, &err_r);
    free (xl_tmp);

    if (status != GSL_SUCCESS)
      {
        return status ;
      }
  }
    
  *result = res_l + res_r;
  *abserr = sqrt (err_l * err_l + err_r * err_r);

  return GSL_SUCCESS;
}

gsl_monte_miser_state *
gsl_monte_miser_alloc (size_t dim)
{
  gsl_monte_miser_state *s =
    (gsl_monte_miser_state *) malloc (sizeof (gsl_monte_miser_state));

  if (s == 0)
    {
      GSL_ERROR_VAL ("failed to allocate space for miser state struct",
		     GSL_ENOMEM, 0);
    }

  s->x = (double *) malloc (dim * sizeof (double));

  if (s->x == 0)
    {
      free (s);
      GSL_ERROR_VAL ("failed to allocate space for x", GSL_ENOMEM, 0);
    }

  s->xmid = (double *) malloc (dim * sizeof (double));

  if (s->xmid == 0)
    {
      free (s->x);
      free (s);
      GSL_ERROR_VAL ("failed to allocate space for xmid", GSL_ENOMEM, 0);
    }

  s->sigma_l = (double *) malloc (dim * sizeof (double));

  if (s->sigma_l == 0)
    {
      free (s->xmid);
      free (s->x);
      free (s);
      GSL_ERROR_VAL ("failed to allocate space for sigma_l", GSL_ENOMEM, 0);
    }

  s->sigma_r = (double *) malloc (dim * sizeof (double));

  if (s->sigma_r == 0)
    {
      free (s->sigma_l);
      free (s->xmid);
      free (s->x);
      free (s);
      GSL_ERROR_VAL ("failed to allocate space for sigma_r", GSL_ENOMEM, 0);
    }

  s->fmax_l = (double *) malloc (dim * sizeof (double));

  if (s->fmax_l == 0)
    {
      free (s->sigma_r);
      free (s->sigma_l);
      free (s->xmid);
      free (s->x);
      free (s);
      GSL_ERROR_VAL ("failed to allocate space for fmax_l", GSL_ENOMEM, 0);
    }

  s->fmax_r = (double *) malloc (dim * sizeof (double));

  if (s->fmax_r == 0)
    {
      free (s->fmax_l);
      free (s->sigma_r);
      free (s->sigma_l);
      free (s->xmid);
      free (s->x);
      free (s);
      GSL_ERROR_VAL ("failed to allocate space for fmax_r", GSL_ENOMEM, 0);
    }

  s->fmin_l = (double *) malloc (dim * sizeof (double));

  if (s->fmin_l == 0)
    {
      free (s->fmax_r);
      free (s->fmax_l);
      free (s->sigma_r);
      free (s->sigma_l);
      free (s->xmid);
      free (s->x);
      free (s);
      GSL_ERROR_VAL ("failed to allocate space for fmin_l", GSL_ENOMEM, 0);
    }

  s->fmin_r = (double *) malloc (dim * sizeof (double));

  if (s->fmin_r == 0)
    {
      free (s->fmin_l);
      free (s->fmax_r);
      free (s->fmax_l);
      free (s->sigma_r);
      free (s->sigma_l);
      free (s->xmid);
      free (s->x);
      free (s);
      GSL_ERROR_VAL ("failed to allocate space for fmin_r", GSL_ENOMEM, 0);
    }

  s->fsum_l = (double *) malloc (dim * sizeof (double));

  if (s->fsum_l == 0)
    {
      free (s->fmin_r);
      free (s->fmin_l);
      free (s->fmax_r);
      free (s->fmax_l);
      free (s->sigma_r);
      free (s->sigma_l);
      free (s->xmid);
      free (s->x);
      free (s);
      GSL_ERROR_VAL ("failed to allocate space for fsum_l", GSL_ENOMEM, 0);
    }

  s->fsum_r = (double *) malloc (dim * sizeof (double));

  if (s->fsum_r == 0)
    {
      free (s->fsum_l);
      free (s->fmin_r);
      free (s->fmin_l);
      free (s->fmax_r);
      free (s->fmax_l);
      free (s->sigma_r);
      free (s->sigma_l);
      free (s->xmid);
      free (s->x);
      free (s);
      GSL_ERROR_VAL ("failed to allocate space for fsum_r", GSL_ENOMEM, 0);
    }

  s->fsum2_l = (double *) malloc (dim * sizeof (double));

  if (s->fsum2_l == 0)
    {
      free (s->fsum_r);
      free (s->fsum_l);
      free (s->fmin_r);
      free (s->fmin_l);
      free (s->fmax_r);
      free (s->fmax_l);
      free (s->sigma_r);
      free (s->sigma_l);
      free (s->xmid);
      free (s->x);
      free (s);
      GSL_ERROR_VAL ("failed to allocate space for fsum2_l", GSL_ENOMEM, 0);
    }

  s->fsum2_r = (double *) malloc (dim * sizeof (double));

  if (s->fsum2_r == 0)
    {
      free (s->fsum2_l);
      free (s->fsum_r);
      free (s->fsum_l);
      free (s->fmin_r);
      free (s->fmin_l);
      free (s->fmax_r);
      free (s->fmax_l);
      free (s->sigma_r);
      free (s->sigma_l);
      free (s->xmid);
      free (s->x);
      free (s);
      GSL_ERROR_VAL ("failed to allocate space for fsum2_r", GSL_ENOMEM, 0);
    }


  s->hits_r = (size_t *) malloc (dim * sizeof (size_t));

  if (s->hits_r == 0)
    {
      free (s->fsum2_r);
      free (s->fsum2_l);
      free (s->fsum_r);
      free (s->fsum_l);
      free (s->fmin_r);
      free (s->fmin_l);
      free (s->fmax_r);
      free (s->fmax_l);
      free (s->sigma_r);
      free (s->sigma_l);
      free (s->xmid);
      free (s->x);
      free (s);
      GSL_ERROR_VAL ("failed to allocate space for fsum2_r", GSL_ENOMEM, 0);
    }

  s->hits_l = (size_t *) malloc (dim * sizeof (size_t));

  if (s->hits_l == 0)
    {
      free (s->hits_r);
      free (s->fsum2_r);
      free (s->fsum2_l);
      free (s->fsum_r);
      free (s->fsum_l);
      free (s->fmin_r);
      free (s->fmin_l);
      free (s->fmax_r);
      free (s->fmax_l);
      free (s->sigma_r);
      free (s->sigma_l);
      free (s->xmid);
      free (s->x);
      free (s);
      GSL_ERROR_VAL ("failed to allocate space for fsum2_r", GSL_ENOMEM, 0);
    }


  s->plain_state = gsl_monte_plain_alloc (dim);

  if (s->plain_state == 0)
    {
      free (s->hits_l);
      free (s->hits_r);
      free (s->fsum2_r);
      free (s->fsum2_l);
      free (s->fsum_r);
      free (s->fsum_l);
      free (s->fmin_r);
      free (s->fmin_l);
      free (s->fmax_r);
      free (s->fmax_l);
      free (s->sigma_r);
      free (s->sigma_l);
      free (s->xmid);
      free (s->x);
      free (s);
      GSL_ERROR_VAL ("failed to allocate plain_state", GSL_ENOMEM, 0);
    }

  gsl_monte_plain_init (s->plain_state);
  gsl_monte_miser_init (s);

  s->dim = dim;

  return s;
}

int
gsl_monte_miser_init (gsl_monte_miser_state * s)
{
  s->min_calls = 15;
  s->min_calls_per_bisection = 60;
  s->estimate_frac = 0.1;
  s->alpha = 2.0;
  s->dither = 0.0;
  s->estimate_style = ESTIMATE_STYLE_NR;

  return GSL_SUCCESS;
}

void
gsl_monte_miser_free (gsl_monte_miser_state * s)
{
  gsl_monte_plain_free (s->plain_state);
  free (s->fsum2_r);
  free (s->fsum2_l);
  free (s->fsum_r);
  free (s->fsum_l);
  free (s->fmin_r);
  free (s->fmin_l);
  free (s->fmax_r);
  free (s->fmax_l);
  free (s->sigma_r);
  free (s->sigma_l);
  free (s->xmid);
  free (s->x);
  free (s);
}

#ifdef JUNK
void
estimate_maxmin (gsl_monte_function * f,
                 const double xl[], const double xu[],
                 size_t dim, size_t calls,
                 gsl_rng * r,
                 gsl_monte_miser_state * state,
                 double *result, double *abserr,
                 const double xmid[],
                 double sigma_l[], double sigma_r[])
{
  /* NR way */
  
  for (i = 0; i < dim; i++)
    {
      fmin_l[i] = fmin_r[i] = GSL_DBL_MAX;
      fmax_l[i] = fmax_r[i] = -GSL_DBL_MAX;
    }
  
  for (n = 0; n < calls; n++)
    {
      for (i = 0; i < dim; i++)
        {
          state->x[i] = xl[i] + gsl_rng_uniform_pos (r) * (xu[i] - xl[i]);
        }
      
      fval = GSL_MONTE_FN_EVAL(f, x);
      
      for (i = 0; i < dim; i++)
        {
          if (x[i] <= xmid[i])
            {
              fmin_l[i] = GSL_MIN (fmin_l[i], fval);
              fmax_l[i] = GSL_MAX (fmax_l[i], fval);
            }
          else
            {
              fmin_r[i] = GSL_MIN (fmin_r[i], fval);
              fmax_r[i] = GSL_MAX (fmax_r[i], fval);
            }
        }
    }
  
  for (i = 0; i < dim; i++)
    {
      if (fmax_l[i] >= fmin_l[i] && fmax_r[i] >= fmin_r[i])
        {
          sigma_l[i] = GSL_MAX (GSL_MACH_EPS, fmax_l[i] - fmin_l[i]);
          sigma_r[i] = GSL_MAX (GSL_MACH_EPS, fmax_r[i] - fmin_r[i]);
        }
      else
        {
          /* must be that no points landed in one of the half-regions */
          sigma_l[i] = sigma_r[i] = -1;
        }
    }
}
#endif

int
estimate_corrmc (gsl_monte_function * f,
                 const double xl[], const double xu[],
                 size_t dim, size_t calls,
                 gsl_rng * r,
                 gsl_monte_miser_state * state,
                 double * result, double * abserr,
                 const double xmid[],
                 double sigma_l[], double sigma_r[])
{
  size_t i, n;

  double *x = state->x;
  double *fsum_l = state->fsum_l;
  double *fsum_r = state->fsum_r;
  double *fsum2_l = state->fsum2_l;
  double *fsum2_r = state->fsum2_r;
  size_t *hits_l = state->hits_l;
  size_t *hits_r = state->hits_r;

  double vol = 1;

  for (i = 0; i < dim; i++)
    {
      vol *= xu[i] - xl[i];
      hits_l[i] = hits_r[i] = 0;
      fsum_l[i] = fsum_r[i] = 0.0;
      fsum2_l[i] = fsum2_r[i] = 0.0;
      sigma_l[i] = sigma_r[i] = -1;
    }
  
  for (n = 0; n < calls; n++)
    {
      double fval;

      for (i = 0; i < dim; i++)
        {
          x[i] = xl[i] + gsl_rng_uniform_pos (r) * (xu[i] - xl[i]);
        }
      
      fval = GSL_MONTE_FN_EVAL(f, x);

      for (i = 0; i < dim; i++)
        {
          if (x[i] <= xmid[i])
            {
              fsum_l[i] += fval;
              fsum2_l[i] = fval * fval;
              hits_l[i]++;
            }
          else
            {
              fsum_r[i] += fval;
              fsum_r[i] = fval * fval;
              hits_r[i]++;
            }
        }
    }
  
  for (i = 0; i < dim; i++)
    {
      double fraction_l = (xmid[i] - xl[i]) / (xu[i] - xl[i]);
      
      if (hits_l[i] > 0)
        {
          fsum_l[i] /= hits_l[i];
          sigma_l[i] = sqrt (fsum2_l[i] - fsum_l[i] * fsum_l[i] / hits_l[i]);
          sigma_l[i] *= fraction_l * vol / hits_l[i];
        }
      
      if (hits_r[i] > 0)
        {
          fsum_r[i] /= hits_r[i];
          sigma_r[i] = sqrt (fsum2_r[i] - fsum_r[i] * fsum_r[i] / hits_r[i]);
          sigma_r[i] *= (1 - fraction_l) * vol / hits_r[i];
        }
    }

}
