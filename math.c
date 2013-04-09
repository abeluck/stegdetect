/*
 * Copyright 2002 Niels Provos <provos@citi.umich.edu>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Niels Provos.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/queue.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <err.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

#include <jpeglib.h>

#include "config.h"
#include "common.h"

#define SWAP(a, b) do { \
	double tmp; \
	tmp = (a); (a) = (b); (b) = tmp; \
} while (0)

void
matrix_invert(double **a, int n)
{
	double big, pivinv, tmp;
	int i, icol, irow, j, k, l, ll;

	int *indxc, *indxr, *ipiv;

	indxc = calloc(n, sizeof(int));
	indxr = calloc(n, sizeof(int));
	ipiv = calloc(n, sizeof(int));

	if (indxc == NULL || indxr == NULL || ipiv == NULL)
		err(1, "%s: malloc", __func__);

	for (i = 0; i < n; i++) {
		big = 0.0;
		for (j = 0; j < n; j++) {
			if (ipiv[j] == 1)
				continue;

			for (k = 0; k < n; k++) {
				if (ipiv[k] == 0) {
					if (fabs(a[j][k]) >= big) {
						big = fabs(a[j][k]);
						irow = j;
						icol = k;
					}
				} else if (ipiv[k] > 1)
					err(1, "%s: singular matrix",
					    __func__);
			}
		}
		++ipiv[icol];
		if (irow != icol) {
			for (l = 0; l < n; l++)
				SWAP(a[irow][l], a[icol][l]);
		}
		indxr[i] = irow;
		indxc[i] = icol;
		if (a[icol][icol] == 0.0)
			err(1, "%s: singular matrix", __func__);
		pivinv = 1.0 / a[icol][icol];
		a[icol][icol] = 1.0;
		for (l = 0; l < n; l++)
			a[icol][l] *= pivinv;
		for (ll = 0; ll < n; ll++)
			if (ll != icol) {
				tmp = a[ll][icol];
				a[ll][icol] = 0.0;
				for (l = 0; l < n; l++)
					a[ll][l] -= a[icol][l] * tmp;
			}
	}
	for (l = n - 1; l >= 0; l--) {
		if (indxr[l] != indxc[l])
			for (k = 0; k < n; k++)
				SWAP(a[k][indxr[l]], a[k][indxc[l]]);
	}

	free(ipiv);
	free(indxr);
	free(indxc);
}
#undef SWAP

void
print_matrix(char *str, double **A, int m, int n )
{
	int i, j;

	printf("%s:  (%d x %d)\n", str, m, n);
	for (i = 0; i < m; i++) {
		printf(">");
		for (j = 0; j < n; j++) {
			printf(" %4.2f", A[ i ][ j ]);
		}
		printf("\n");
	}
}

void
test_matrix(void)
{
	double *A[2], B[2] = {1, 1.5}, C[2] = {0, 2};

	A[0] = B;
	A[1] = C;

	print_matrix("A", A, 2, 2);

	matrix_invert(A, 2);

	print_matrix("A", A, 2, 2);
}
