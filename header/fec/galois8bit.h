#ifndef _GALOIS8BIT_H_ 
#define _GALOIS8BIT_H_

class galois8bit
{
public:
	galois8bit(void);
	~galois8bit(void);
public:
	unsigned char galoisAdd (unsigned char A, unsigned char B);
	unsigned char galoisSub (unsigned char A, unsigned char B);
	unsigned char galoisMul (unsigned char A, unsigned char B);
	unsigned char galoisDiv (unsigned char A, unsigned char B);
	unsigned char galoisPow (unsigned char A, unsigned char B);
	unsigned char galoisInv (unsigned char A);
	void galoisEightBitInit(void);
};

#endif
