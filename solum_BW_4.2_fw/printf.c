#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include "fw.h"



typedef void (*StrPrintfExCbk)(void* userData, char chr);

static unsigned StrPrvPrintfEx_number(StrPrintfExCbk putc_f, void* userData, unsigned long number, bool base10, bool zeroExtend, bool isSigned, unsigned padToLength, char caps)
{
	char buf[64];
	unsigned idx = sizeof(buf) - 1;
	unsigned chr, i;
	char neg = 0;
	unsigned numPrinted = 0;
	
	if (padToLength > 63)
		padToLength = 63;
	
	buf[idx--] = 0;	//terminate
	
	if (isSigned) {
		
		if (number & 0x80000000UL) {
			
			neg = 1;
			number = -number;
		}
	}
	
	do {
		if (base10) {
			chr = number % 10;
			number = number / 10;
		}
		else {
			chr = number & 0x0F;
			number >>= 4;
		}
		
		buf[idx--] = (chr >= 10) ? (chr + (caps ? 'A' : 'a') - 10) : (chr + '0');
		
		numPrinted++;
		
	} while(number);
	
	if (neg) {
		
		buf[idx--] = '-';
		numPrinted++;
	}
	
	if (padToLength > numPrinted)
		padToLength -= numPrinted;
	else
		padToLength = 0;
	
	while (padToLength--) {
		
		buf[idx--] = zeroExtend ? '0' : ' ';
		numPrinted++;
	}
	
	idx++;
	
	
	for (i = 0; i < numPrinted; i++)
		putc_f(userData,(buf + idx)[i]);
	
	return numPrinted;
}

static unsigned StrVPrintf_StrLen_withMax(const char* s, unsigned max)
{
	unsigned len = 0;
	
	while((*s++) && (len < max))
		len++;
	
	return len;
}

static void StrVPrintfEx(StrPrintfExCbk putc_f, void *userData, const char *fmtStr, va_list vl)
{
	unsigned long val64;
	unsigned i;
	char c;
		
	while ((c = *fmtStr++) != 0) {
		
		if (c == '%') {
			
			bool zeroExtend = false, useLong = false, caps = false;
			unsigned padToLength = 0, len, i;
			const char *str;
			
more_fmt:
			
			switch (c = *fmtStr++) {
				
				case '%':
					
					putc_f(userData, c);
					break;
				
				case 'c':
					
					putc_f(userData, (char)va_arg(vl, unsigned int));
					break;
				
				case 's':
					
					str = va_arg(vl,char*);
					if (!str)
						str = "(null)";
					if (padToLength)
						len = StrVPrintf_StrLen_withMax(str,padToLength);
					else
						padToLength = len = strlen(str);
					if (len > padToLength)
						len = padToLength;
					else{
						
						for(i = len; i < padToLength; i++)
							putc_f(userData,' ');
					}
					for(i = 0; i < len; i++)
						putc_f(userData, *str++);
					break;
				
				case '0':
					
					if (!zeroExtend && !padToLength) {
						
						zeroExtend = true;
						goto more_fmt;
					}
				
				case '1':
				case '2':
				case '3':
				case '4':
				case '5':
				case '6':
				case '7':
				case '8':
				case '9':
					
					padToLength = (padToLength * 10) + c - '0';
					goto more_fmt;
				
				case 'u':
					
					val64 = useLong ? va_arg(vl,unsigned long) : va_arg(vl,unsigned);
					StrPrvPrintfEx_number(putc_f, userData, val64, true, zeroExtend, false, padToLength, false);
					break;
					
				case 'd':
					
					val64 = useLong ? va_arg(vl,unsigned long) : (signed long)va_arg(vl,signed);
					StrPrvPrintfEx_number(putc_f, userData, val64 ,true, zeroExtend, true, padToLength, false);
					break;
					
				case 'X':
					caps = true;
					
				case 'x':
					
					val64 = useLong ? va_arg(vl,unsigned long) : va_arg(vl,unsigned);
					StrPrvPrintfEx_number(putc_f, userData, val64, false, zeroExtend, false, padToLength, caps);
					break;
					
				case 'l':
					useLong = true;
					goto more_fmt;
				
				default:
					putc_f(userData,c);
					break;
			}
		}
		else
			putc_f(userData,c);
	}
}

static void prPutchar(void *userData, char chr)
{
	(void)userData;
	
	uartTx(1, &chr, 1);
}

void pr(const char *fmt, ...)
{
	va_list vl;
	
	va_start(vl, fmt);
	StrVPrintfEx(prPutchar, NULL, fmt, vl);
	va_end(vl);
}

struct VsnprintfData {
	char *dst;
	char *dstEnd;
};

static void snprintfPutchar(void *userData, char chr)
{
	struct VsnprintfData *d = (struct VsnprintfData*)userData;
	
	if (d->dst < d->dstEnd)
		*d->dst++ = chr;
}

int vsnprintf (char *s, size_t n, const char *fmt, va_list vl)
{
	struct VsnprintfData d = {.dst = s, .dstEnd = s + n};
	int ret;
	
	StrVPrintfEx(snprintfPutchar, &d, fmt, vl);
	ret = d.dst - s;
	
	if (ret < n) {
		*d.dst = 0;
		ret++;
	}
	return ret;
}