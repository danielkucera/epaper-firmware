#ifndef _DISPLAY_H_
#define _DISPLAY_H_

#include <stdbool.h>

#define DISPLAY_WIDTH		(400)
#define DISPLAY_HEIGHT		(300)
#define DISPLAY_BPP			(2)

#if defined(TAG_BW)
	#define DISPLAY_TYPE	(TagScreenEink_BW_2bpp)
#elif defined(TAG_BWR)
	#define DISPLAY_TYPE	(TagScreenEink_BWR_only)
#endif

void displayInit(void);
bool displayRefresh(bool partial);	//partial only supported in 2bpp mode
void *displayGetFbPtr(void);



#endif
