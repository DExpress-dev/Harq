#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "matrix.h"

matrix::matrix(void)
{
}

matrix::~matrix(void)
{
}

matrix_t matrix::matrixAdd (matrix_t *A, matrix_t *B) 
{
	assert (A && B);
	assert (A->m_col == B->m_col);
	assert (A->m_row == B->m_row);

	int i,j;
	matrix_t r;
	unsigned char tmp;
	r.m_col = A->m_col;
	r.m_row = A->m_row;

	for (i = 0; i < r.m_row; ++i) 
	{
		for (j = 0; j < r.m_col; ++j) 
		{
			tmp = galois8bit_ptr_.galoisAdd(A->m_data[i][j],B->m_data[i][j]);
			r.m_data[i][j] = tmp;
		}
	}
	return r;
}

matrix_t matrix::matrixSub (matrix_t *A, matrix_t *B) 
{
	assert (A && B);
	assert (A->m_col == B->m_col);
	assert (A->m_row == B->m_row);

	int i, j;
	matrix_t r;
	unsigned char tmp;
	r.m_col = A->m_col;
	r.m_row = A->m_row;

	for (i = 0; i < r.m_row; ++i) 
	{
		for (j = 0; j < r.m_col; ++j) 
		{
			tmp = galois8bit_ptr_.galoisSub(A->m_data[i][j],B->m_data[i][j]);
			r.m_data[i][j] = tmp;
		}
	}
	return r;
}

matrix_t matrix::matrixMul (matrix_t *A, matrix_t *B) 
{
	assert (A && B);
	assert (A->m_col == B->m_row);

	int i, j, k;
	unsigned char sum, tmp;
	matrix_t r;
	r.m_row = A->m_row;
	r.m_col = B->m_col;
	
	for (i = 0; i < A->m_row; ++i) 
	{
		for (j = 0; j < B->m_col; ++j) 
		{
			sum = 0;
			for (k = 0; k < A->m_col; ++j) 
			{
				tmp = galois8bit_ptr_.galoisMul(A->m_data[i][k],B->m_data[k][j]);
				sum = galois8bit_ptr_.galoisAdd(sum, tmp);
			}
			r.m_data[i][j] = sum;
		}
	}
	return r;
}

matrix_t matrix::matrixNumMul (matrix_t *A, unsigned char k) 
{
	assert (A);
	
	int i, j;
	matrix_t r;
	r.m_row = A->m_row;
	r.m_col = A->m_col;
	
	for (i = 0; i < A->m_row; ++i) 
	{
		for (j = 0; j < A->m_col; ++j) 
		{
			r.m_data[i][j] = galois8bit_ptr_.galoisMul(k, A->m_data[i][j]);
		}
	}
	return r;
}

matrix_t matrix::matrixTrans (matrix_t *A) 
{
	assert (A);

	int i, j;
	matrix_t r;
	r.m_row = A->m_col;
	r.m_col = A->m_row;

	for (i = 0; i < A->m_row; ++i) 
	{
		for (j = 0; j < A->m_col; ++j) 
		{
			r.m_data[j][i] = A->m_data[i][j];
		}
	}
	return r;
}

matrix_t matrix::matrixGauss (matrix_t *A) 
{
	int i, j, k, max, pos, len;
	unsigned char tmpdiv, tmpmul, tmpinv;
	unsigned char swaparr[MAX] = {0};
	matrix_t src, dest;
	dest.m_row = A->m_row;
	dest.m_col = A->m_col;
	memcpy (&src, A, sizeof(matrix_t));
	memset (dest.m_data, 0, sizeof(unsigned char) * MAX * MAX);
	
	for (i = 0; i < dest.m_row; ++i) 
	{
		dest.m_data[i][i] = 1;
	}
	
	for (k = 0; k < src.m_col; ++k) 
	{
		max = src.m_data[k][k];
		pos = k;
		for (i = k+1; i<src.m_row; ++i) 
		{
			if (src.m_data[i][k] > max) 
			{
				pos = i;
				max = src.m_data[i][k];
			}
		}
		if (pos != k) 
		{  // swap the pos row and the k row
			len = src.m_col*sizeof(unsigned char);
			memcpy (swaparr, src.m_data[pos], len);
			memcpy (src.m_data[pos], src.m_data[k], len);
			memcpy (src.m_data[k], swaparr, len);
			memcpy (swaparr, dest.m_data[pos], len);
			memcpy (dest.m_data[pos], dest.m_data[k], len);
			memcpy (dest.m_data[k], swaparr, len);
		}
		for (i = 0; i < src.m_row; ++i) 
		{
			if (i != k) 
			{
				tmpdiv = galois8bit_ptr_.galoisDiv(src.m_data[i][k], src.m_data[k][k]);
				for (j = 0; j < src.m_col; ++j) 
				{
					tmpmul = galois8bit_ptr_.galoisMul(tmpdiv, src.m_data[k][j]);
					src.m_data[i][j] = galois8bit_ptr_.galoisAdd(tmpmul, src.m_data[i][j]);
					tmpmul = galois8bit_ptr_.galoisMul(tmpdiv, dest.m_data[k][j]);
					dest.m_data[i][j] = galois8bit_ptr_.galoisAdd(tmpmul, dest.m_data[i][j]);
				}
			}
		}
	}
	for (i = 0; i < dest.m_row; ++i) 
	{
		tmpinv = galois8bit_ptr_.galoisInv(src.m_data[i][i]);
		for (j = 0; j < dest.m_col; ++j)
		{
			dest.m_data[i][j] = galois8bit_ptr_.galoisMul(dest.m_data[i][j], tmpinv);
		}
	}
	return dest;
}
