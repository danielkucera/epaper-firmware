#include <string.h>
#include "mz100.h"
#include "ccm.h"
#include "fw.h"


static bool aesCcmOp(void* dst, const void *src, uint16_t authSrcLen, uint16_t encSrcLen, const void *key, const void *nonce, bool dec)
{
	uint32_t tempB, nBytesNoMic = authSrcLen + encSrcLen, nBytesIn, nBytesOut;
	const uint32_t *inD = (const uint32_t*)src;
	uint32_t *outD = (uint32_t*)dst;
	uint_fast8_t i;
	bool success;

#ifdef AES_DEBUG
	pr("\naes dec: %d src: %x dst: %x \n", dec, &src, &dst);
	pr("authSrcLen: %d encSrcLen: %d \n", authSrcLen, encSrcLen);
	pr("src:");
	for (int i=0; i<authSrcLen+encSrcLen; i++){
		pr("%02X", ((unsigned char *)src)[i]);
	}
	pr("\n");
	pr("key:");
	for (int i=0; i<16; i++){
		pr("%02X", ((unsigned char *)key)[i]);
	}
	pr("\n");
	pr("nonce:");
	for (int i=0; i<AES_CCM_NONCE_SIZE; i++){
		pr("%02X", ((unsigned char *)nonce)[i]);
	}
	pr("\n");
#endif

	if (dec) {
		nBytesIn = nBytesNoMic + AES_CCM_MIC_SIZE;
		nBytesOut = nBytesNoMic;
	}
	else {
		nBytesIn = nBytesNoMic;
		nBytesOut = nBytesNoMic + AES_CCM_MIC_SIZE;
	}
	
	AES->CTRL1 &=~ 128; // reset HW prio
	AES->CTRL1 |= 64; // set MCU prio
	
	//while (!(AES->STATUS & 1)); // DONE

	do{
		//AES->CTRL1 = (AES->CTRL1 &~ 0x0d) | 0x02; // reset: START, IF_CLR, OF_CLR; set LOCK0
		AES->CTRL1 |= 0x02; // set LOCK0
	} while (!(AES->STATUS & 2)); // RSVD0
	
	AES->CTRL2 |= 1; // AES_RESET
	for(uint32_t localCnt = 0; localCnt < 0x20; localCnt++); //wait
	AES->CTRL2 &=~ 1; // AES_RESET

	AES->CTRL1 = (AES->CTRL1 &~ 0x0d) | 0x02; // reset: START, IF_CLR, OF_CLR; set LOCK0
	
	AES->IMR = 0x07; // disable all interrupt

	AES->CTRL1 = 0x0005501e + (dec ? 0x8000 : 0); // MODE=CCM; OUT_MIC=1; MIC_LEN=4b; OUT_MSG=1; IF_CLR=OF_CLR=1; LOCK0=1 

	for(i = 0; i < 4; i++)
		AES->KEY[7 - i] = ((uint32_t*)key)[i];
	
	AES->MSTR_LEN = encSrcLen;
	AES->ASTR_LEN = authSrcLen;

	for(i = 0; i < 3; i++)
		AES->IV[i] = ((uint32_t*)nonce)[i];
	AES->IV[3] = ((uint8_t*)nonce)[12] + 0x200;	//2 byte lengths
	
	AES->CTRL1 |= 1; // START
	
	uint32_t nretries;
	
	while (nBytesIn || nBytesOut) {
		
		if (!(AES->STATUS & 0x10) && nBytesIn) { // !IF_FULL & bytes left to copy
			
			if (nBytesIn >= 4) { // 4 or more bytes left
				//pr("i %0X \n", (*inD));
				AES->STR_IN = *inD++; // copy 1 word
				nBytesIn -= 4; // 
			}
			else {
				tempB = 0; // use temp var
				memcpy(&tempB, inD, nBytesIn); //copy bytes left
				AES->STR_IN = tempB;
				nBytesIn = 0; // copy input complete 
			}
		}
		
		if ((AES->STATUS & 0x40) && nBytesOut) { // OF_RDY & bytes left to read
			
			tempB = AES->STR_OUT;
			//pr("o %0X \n", tempB);

			if (nBytesOut >= 4) {
				*outD++ = tempB; // copy output
				nBytesOut -= 4; // decrease bytes left
			}
			else {
				memcpy(outD, &tempB, nBytesOut);
				nBytesOut = 0;
			}
			
		}
		
		if (nretries++ > 1000000) {
			pr("AES timeout. in: %d out :%d\n", nBytesIn, nBytesOut);
			while(1);
		}
	}
	
#ifdef AES_DEBUG
	pr("\naes res: %d :\n", dec);
	for (int i=0; i<encSrcLen+authSrcLen; i++){
		pr("%02X", ((unsigned char *)dst)[i]);
	}
	pr("\n");
#endif

	success = !((AES->STATUS >> 11) & 7); // STATUS 0=no error
	
	AES->CTRL1 &=~ 0x02; // reset LOCK0
	
	return success;
}

void aesCcmEnc(void* dst, const void *src, uint16_t authSrcLen, uint16_t encSrcLen, const void *key, const void *nonce)
{
	aesCcmOp(dst, src, authSrcLen, encSrcLen, key, nonce, false);
}

bool aesCcmDec(void* dst, const void *src, uint16_t authSrcLen, uint16_t encSrcLen, const void *key, const void *nonce)
{
	return aesCcmOp(dst, src, authSrcLen, encSrcLen, key, nonce, true);
}




