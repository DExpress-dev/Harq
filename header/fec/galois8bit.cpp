#include <stdio.h>
#include "galois8bit.h"

#define NUM 8
#define POLYNOMIAL (1<<4|1<<3|1<<2|1)

unsigned char GaloisValue[1 << NUM];
unsigned char GaloisIndex[1 << NUM];

galois8bit::galois8bit()
{
}

galois8bit::~galois8bit(void)
{
}

void galois8bit::galoisEightBitInit(void) 
{
	int i, j = 1;
	for (i = 0; i < (1<<NUM)-1; ++i) 
	{
		GaloisValue[i] = j;
		GaloisIndex[GaloisValue[i]] = i;
		if (j & 0x80)
		{
			j <<= 1;
			j ^= POLYNOMIAL;
		}
		else
		{
			j <<= 1;
		}
	}
}

unsigned char galois8bit::galoisAdd(unsigned char A, unsigned char B)
{
	return A ^ B;
}

unsigned char galois8bit::galoisSub(unsigned char A, unsigned char B)
{
	return A ^ B;
}

unsigned char galois8bit::galoisMul(unsigned char A, unsigned char B) 
{
	if (!A || !B) 
	{
		return 0;
	}
	unsigned char i = GaloisIndex[A];
	unsigned char j = GaloisIndex[B];
	unsigned char index = (i+j)%255;
	return GaloisValue[index];
}

unsigned char galois8bit::galoisDiv(unsigned char A, unsigned char B)
{
	if (!A || !B) 
	{
		return 0;
	}
	return galoisMul(A, galoisInv(B));
}

unsigned char galois8bit::galoisPow(unsigned char A, unsigned char B)
{
	if (!A)
	{
		return 0;
	}
	if (!B)
	{
		return 1;
	}
	unsigned char i, r = 1;
	for (i = 0; i < B; ++i)
	{
		r = galoisMul(r, A);
	}
	return r;
}

unsigned char galois8bit::galoisInv(unsigned char A) 
{
	if (!A)
	{
		return 0;
	}
	unsigned char j = GaloisIndex[A];
	return GaloisValue[(255-j)%255];
}


