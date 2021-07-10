#include "asmUtil.h"
#include "screen.h"
#include "eeprom.h"
#include "cc111x.h"
#include "printf.h"
#include "u1.h"

static uint32_t __xdata mEepromSize;
static uint8_t __xdata mOpcodeErz4K = 0, mOpcodeErz32K = 0, mOpcodeErz64K = 0;

#ifdef EPOP900

	#pragma callee_saves eepromByte
	static inline uint8_t eepromByte(uint8_t v)
	{
		U0DBUF = v;
		while (!(U0CSR & 0x02));
		U0CSR &= (uint8_t)~0x02;
		while (U0CSR & 0x01);
		return U0DBUF;
	}
	
#else
	#define eepromByte u1byte
#endif


uint32_t eepromGetSize(void)
{
	return mEepromSize;
}

#pragma callee_saves eepromPrvSelect
static inline void eepromPrvSelect(void)
{
	__asm__("nop");
	P2 &= (uint8_t)~(1 << 0);
	__asm__("nop");
}

#pragma callee_saves eepromPrvDeselect
static inline void eepromPrvDeselect(void)
{
	__asm__("nop");
	P2 |= 1 << 0;
	__asm__("nop");
}

void eepromReadStart(uint32_t addr) __reentrant
{
	eepromPrvSelect();
	eepromByte(0x03);
	eepromByte(addr >> 16);
	eepromByte(addr >> 8);
	eepromByte(addr & 0xff);
}

void eepromRead(uint32_t addr, void __xdata *dstP, uint16_t len) __reentrant
{
	uint8_t __xdata *dst = (uint8_t __xdata*)dstP;
	
	eepromPrvSelect();
	eepromByte(0x03);
	eepromByte(addr >> 16);
	eepromByte(addr >> 8);
	eepromByte(addr & 0xff);
	
	while (len--)
		*dst++ = eepromByte(0);
	eepromPrvDeselect();
}

static void eepromPrvSimpleCmd(uint8_t cmd)
{
	eepromPrvSelect();
	eepromByte(cmd);
	eepromPrvDeselect();
}

static bool eepromPrvBusyWait(void)
{
	uint8_t val;
	
	eepromPrvSelect();
	eepromByte(0x05);
	while ((val = eepromByte(0x00)) & 1);
	eepromPrvDeselect();
	
	return true;
}

static bool eepromWriteLL(uint32_t addr, const void __xdata *srcP, uint16_t len)
{
	const uint8_t __xdata *src = (const uint8_t __xdata*)srcP;
	
	eepromPrvSimpleCmd(0x06);
	
	eepromPrvSelect();
	eepromByte(0x02);
	eepromByte(addr >> 16);
	eepromByte(addr >> 8);
	eepromByte(addr & 0xff);
	
	while (len--)
		eepromByte(*src++);
	eepromPrvDeselect();
	
	return eepromPrvBusyWait();
}

void eepromDeepPowerDown(void)
{
	eepromPrvSimpleCmd(0xb9);
}

static void eepromPrvWakeFromPowerdown(void)
{
	eepromPrvSimpleCmd(0xab);
}

#pragma callee_saves eepromPrvSfdpRead
static void eepromPrvSfdpRead(uint16_t ofst, uint8_t __xdata *dst, uint8_t len)
{
	eepromPrvSelect();
	eepromByte(0x5a);		//cmd
	eepromByte(0);			//addr
	eepromByte(ofst >> 8);
	eepromByte(ofst);
	eepromByte(0x00);		//dummy
	while(len--)
		*dst++ = eepromByte(0);
	eepromPrvDeselect();
}

__bit eepromInit(void)
{
	uint8_t __xdata buf[8];
	uint8_t i, nParamHdrs;
	
	#ifdef EPOP900		//uses uart0
	
		U0BAUD = 0;
		U0GCR = 0x31;
		U0CSR = 0x40;
		P0SEL |= (1 << 2) | (1 << 3) | (1 << 5);
	
	#endif
	
	//P2.0 is nCS
	P2SEL &= (uint8_t)~(1 << 0);
	P2DIR |= 1 << 0;
	P2 |= 1 << 0;
	
	eepromPrvWakeFromPowerdown();
	
	//process SFDP
	
	eepromPrvSfdpRead(0, buf, 8);
	if (buf[0] != 0x53 || buf[1] != 0x46 || buf[2] != 0x44 || buf[3] != 0x50 || buf[7] != 0xff) {
		pr("SFDP: header not found\n");
		
		__bit valid = false;
		
		//try manual ID for chips we know of
		eepromPrvSelect();
		eepromByte(0x90);
		eepromByte(0x00);
		eepromByte(0x00);
		eepromByte(0x00);
		if (eepromByte(0) == 0xc2) { 	//old macronix chips
			valid = true;
			mOpcodeErz4K = 0x20;
			switch (eepromByte(0)) {
				case 0x05:				//MX25V512
					mEepromSize = 0x00010000ul;
					break;
				
				case 0x12:				//MX25V4005
					mEepromSize = 0x00080000ul;
					break;
				
				default:
					valid = false;
					break;
			}
		}
		eepromPrvDeselect();
		
		return valid;
	}
	if (buf[5] != 0x01) {
		pr("SFDP: version wrong: %u.%d\n", buf[5], buf[4]);
		return false;
	}
	nParamHdrs = buf[6];
	if (nParamHdrs == 0xff)	//that case is very unlikely and we just do not care
		nParamHdrs--;
	
	//now we need to find the JEDEC parameter table header
	for (i = 0; i <= nParamHdrs; i++) {
		
		eepromPrvSfdpRead(mathPrvMul8x8(i, 8) + 8, buf, 8);
		if (buf[0] == 0x00 && buf[2] == 0x01 && buf[3] >= 9) {
			
			uint8_t j;
			
			eepromPrvSfdpRead(*(uint16_t __xdata*)(buf + 4), mScreenRow, 9 * 4);
			if ((mScreenRow[0] & 3) != 1) {
				pr("SFDP: no 4K ERZ\n");
				break;
			}
			if (!(mScreenRow[0] & 0x04)) {
				pr("SFDP: no large write buf\n");
				break;
			}
			if ((mScreenRow[2] & 0x06)) {
				pr("SFDP: addr.len != 3\n");
				break;
			}
			
			if (!mScreenRow[1] || mScreenRow[1] == 0xff) {
				pr("SFDP: 4K ERZ opcode invalid\n");
				break;
			}
			mOpcodeErz4K = mScreenRow[1];
			
			if (mScreenRow[7] & 0x80) {
				
				pr("SFDP: device too big\n");
				break;
			}
			else {
				
				if (j = mScreenRow[7])
					mEepromSize = 0x00200000UL;
				else if (j = mScreenRow[6])
					mEepromSize = 0x00002000UL;
				else if (j = mScreenRow[5])
					mEepromSize = 0x00000020UL;
				else {
					pr("SFDP: device so small?!\n");
					break;
				}
				
				while (j) {
					mEepromSize <<= 1;
					j >>= 1;
				}
			}
			
			//get erase opcodes
			for (j = 0x1c; j < 0x24; j += 2) {
				uint8_t instr = mScreenRow[j + 1];
				
				if (!instr || instr == 0xff)
					continue;
				
				switch (mScreenRow[j]) {
					case 0x0c:
						if (mOpcodeErz4K != instr) {
							pr("4K ERZ opcode disagreement\n");
							return false;
						}
						break;
						
					case 0x0f:		//32K erase
						mOpcodeErz32K = instr;
						break;
					
					case 0x10:		//64K erase
						mOpcodeErz64K = instr;
						break;
				}
			}
			
			/*
			pr("EEPROM accepted\n");
			pr(" ERZ opcodes: \n");
			if (mOpcodeErz4K)
				pr(" 4K:  %02xh\n", mOpcodeErz4K);
			if (mOpcodeErz32K)
				pr(" 32K: %02xh\n", mOpcodeErz32K);
			if (mOpcodeErz64K)
				pr(" 64K: %02xh\n", mOpcodeErz64K);
			pr(" Size: 0x%*08lx\n", (uint16_t)&mEepromSize);
			*/
			return true;
		}
	}
	
	pr("SFDP: no JEDEC table of expected version found\n");
	return false;
}

bool eepromWrite(uint32_t addr, const void __xdata *srcP, uint16_t len) __reentrant
{
	const uint8_t __xdata *src = (const uint8_t __xdata*)srcP;
	
	while (len) {
		
		uint16_t lenNow = EEPROM_WRITE_PAGE_SZ - (addr & (EEPROM_WRITE_PAGE_SZ - 1));
		
		if (lenNow > len)
			lenNow = len;
		
		if (!eepromWriteLL(addr, src, lenNow))
			return false;

		addr += lenNow;
		src += lenNow;
		len -= lenNow;
	}
	return true;
}

bool eepromErase(uint32_t addr, uint16_t nSec) __reentrant
{
	uint8_t now;
	
	if (((uint16_t)addr) & 0x0fff)
		return false;
	
	for (;nSec; nSec -= now) {
	
		eepromPrvSimpleCmd(0x06);
		eepromPrvSelect();
		
		if (nSec >= 16 && !(uint16_t)addr && mOpcodeErz64K) {	//erase 64K
			
			eepromByte(mOpcodeErz64K);
			now = 16;
		}
		else if (nSec >= 8 && !(((uint16_t)addr) & 0x7fff) && mOpcodeErz32K) {	//erase 32K
			
			eepromByte(mOpcodeErz32K);
			now = 8;
		}
		else {		//erase 4K
			
			eepromByte(mOpcodeErz4K);
			now = 1;
		}

		eepromByte(addr >> 16);
		eepromByte(addr >> 8);
		eepromByte(addr);
		eepromPrvDeselect();
		
		if (!eepromPrvBusyWait())
			return false;
		
		addr += mathPrvMul16x8(EEPROM_ERZ_SECTOR_SZ, now);
	}
	
	return true;
}

void eepromOtpModeEnter(void)
{
	eepromPrvSimpleCmd(0xb1);
}

void eepromOtpModeExit(void)
{
	eepromPrvSimpleCmd(0xc1);
}