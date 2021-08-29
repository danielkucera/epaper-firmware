#include "display.h"
#include <stdint.h>
#include "timer.h"
#include "heap.h"
#include "fw.h"


#define GPIO_NUM_BS						2
#define GPIO_NUM_nCS					23

#define GPIO_NUM_nRESET					24
#define GPIO_NUM_BUSY					27
#define GPIO_NUM_DnC					26



static void *mFramebuffer;
static bool mInited = false;

void displayInit(void)
{
	mFramebuffer = heapAlloc(((DISPLAY_WIDTH * DISPLAY_BPP + 7) / 8) * DISPLAY_HEIGHT);
}

void *displayGetFbPtr(void)
{
	return mFramebuffer;
}

static bool einkInit(void)
{
	if (mInited)
		return true;
	
	einkInitUsingFw();
	mInited = true;
	
	return true;
}

bool TEXT2 displayRefresh(bool partial)
{
	uint32_t r, c;
	
	if (partial)
		return false;	//no partial support yet
	
	if (!einkInit()) {
		pr("cannot refresh while not inited\n");
		return false;
	}
	
	mInited = false;
	
	gpioSet(GPIO_NUM_BS, 0);
	
	//send B
	einkLlSendCommand(0x10);
	for (r = 0; r < DISPLAY_HEIGHT; r++) {
		for (c = 0; c < DISPLAY_WIDTH / 8; c++) {
					
			uint16_t v = ((uint16_t*)mFramebuffer)[(DISPLAY_HEIGHT - r - 1) * (DISPLAY_WIDTH / 8) + c];
			static const uint8_t lut[] = {0,0,2,2,0,0,2,2,1,1,3,3,1,1,3,3};
			
			einkLlSendData((lut[(v >> 12 & 0x0f)] << 0) | (lut[(v >> 8) & 0x0f] << 2) | (lut[(v >> 4) & 0x0f] << 4) | (lut[(v >> 0) & 0x0f] << 6));
		}
	}
	
	//send R
	einkLlSendCommand(0x13);
	for (r = 0; r < DISPLAY_HEIGHT; r++) {
		for (c = 0; c < DISPLAY_WIDTH / 8; c++) {
					
			uint16_t v = ((uint16_t*)mFramebuffer)[(DISPLAY_HEIGHT - r - 1) * (DISPLAY_WIDTH / 8) + c];
			static const uint8_t lut[] = {3,3,3,1,3,3,3,1,3,3,3,1,2,2,2,0};
			
			einkLlSendData((lut[(v >> 12 & 0x0f)] << 0) | (lut[(v >> 8) & 0x0f] << 2) | (lut[(v >> 4) & 0x0f] << 4) | (lut[(v >> 0) & 0x0f] << 6));
		}
	}
	
	gpioSet(GPIO_NUM_BS, 1);
	
	einkDrawAndPowerOff();
	
	return true;
}

void einkPowerOff(void)
{
	if (mInited)
		einkDrawAndPowerOff();
}

void dispTest(void)
{
	uint8_t *buf = mFramebuffer;
	uint32_t r, c, pos, color;
	
	for (r = 0; r < 300; r++) {
		
		for (c = 0; c < 400; c++) {
			
			pos = r * 400 + c;
			
			color = ((r / 32) + (c / 32)) & 3;
			
			buf[pos / 4] &=~ (3 << ((pos % 4) * 2));
			buf[pos / 4] |= color << ((pos % 4) * 2);
		}
		wdtPet();
	}
	displayRefresh(false);
	
	while(1);
}