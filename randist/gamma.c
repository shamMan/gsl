#include <config.h>
#include <math.h>
#include <gsl_math.h>
#include <gsl_sf.h>
#include <gsl_rng.h>
#include <gsl_randist.h>

static double gamma_large (const gsl_rng * r, double a);
static double gamma_frac (const gsl_rng * r, double a);

/* The Gamma distribution of order a>0 is defined by:

   p(x) dx = {1 / \Gamma(a) b^a } x^{a-1} e^{-x/b} dx

   for x>0.  If X and Y are independent gamma-distributed random
   variables of order a1 and a2 with the same scale parameter b, then
   X+Y has gamma distribution of order a1+a2.

   The algorithms below are from Knuth, vol 2, 2nd ed, p. 129. */

double
gsl_ran_gamma (const gsl_rng * r, const double a, const double b)
{
  /* assume a > 0 */
  unsigned int na = floor (a);

  if (a == na)
    {
      return b * gsl_ran_gamma_int (r, na);
    }
  else if (na == 0)
    {
      return b * gamma_frac (r, a);
    }
  else
    {
      return b * (gsl_ran_gamma_int (r, na) + gamma_frac (r, a - na)) ;
    }
}

double
gsl_ran_gamma_int (const gsl_rng * r, const unsigned int a)
{
  if (a < 12)
    {
      unsigned int i;
      double prod = 1;

      for (i = 0; i < a; i++)
	{
	  prod *= gsl_rng_uniform_pos (r);
	}

      /* Note: for 12 iterations we are safe against underflow, since
	 the smallest positive random number is O(2^-32). This means
	 the smallest possible product is 2^(-12*32) = 10^-116 which
	 is within the range of double precision. */

      return -log (prod);
    }
  else
    {
      return gamma_large (r, (double) a);
    }
}

static double
gamma_large (const gsl_rng * r, const double a)
{
  /* Works only if a > 1, and is most efficient if a is large

     This algorithm, reported in Knuth, is attributed to Ahrens.  A
     faster one, we are told, can be found in: J. H. Ahrens and
     U. Dieter, Computing 12 (1974) 223-246.  */

  double sqa, x, y, v;
  sqa = sqrt (2 * a - 1);
  do
    {
      do
	{
	  y = tan (M_PI * gsl_rng_uniform (r));
	  x = sqa * y + a - 1;
	}
      while (x <= 0);
      v = gsl_rng_uniform (r);
    }
  while (v > (1 + y * y) * exp ((a - 1) * log (x / (a - 1)) - sqa * y));

  return x;
}

static double
gamma_frac (const gsl_rng * r, const double a)
{
  /* This is exercise 16 from Knuth; see page 135, and the solution is
     on page 551.  */

  double p, q, x, u, v;
  p = M_E / (a + M_E);
  do
    {
      u = gsl_rng_uniform (r);
      v = gsl_rng_uniform_pos (r);

      if (u < p)
	{
	  x = exp ((1 / a) * log (v));
	  q = exp (-x);
	}
      else
	{
	  x = 1 - log (v);
	  q = exp ((a - 1) * log (x));
	}
    }
  while (gsl_rng_uniform (r) >= q);

  return x;
}

double
gsl_ran_gamma_pdf (const double x, const double a, const double b)
{
  if (x < 0)
    {
      return 0 ;
    }
  else if (x == 0)
    {
      if (a == 1)
	return 1/b ;
      else
	return 0 ;
    }
  else if (a == 1)
    {
      return exp(-x/b)/b ;
    }
  else 
    {
      double lngamma = gsl_sf_lngamma (a);
      double p = exp ((a - 1) * log (x/b) - x/b - lngamma)/b;
      return p;
    }
}
