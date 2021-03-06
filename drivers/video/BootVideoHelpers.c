/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

// 2002-09-10  agreen@warmcat.com  created

// These are helper functions for displaying bitmap video
// includes an antialiased (4bpp) proportional bitmap font (n x 16 pixel)


#include "boot.h"
#include "video.h"
#include "memory_layout.h"
//#include "string.h"
#include "fontx8.h"
#include <stdarg.h>
#include "decode-jpg.h"
#include "BootVideoHelpers.h"
#define WIDTH_SPACE_PIXELS 5

// returns number of x pixels taken up by ascii character bCharacter

unsigned int BootVideoGetCharacterWidth(u8 bCharacter, bool fDouble) {
    if(fDouble)
        return font8x8_basic_width * 2;
    
    return font8x8_basic_width;
}

// returns number of x pixels taken up by string

unsigned int BootVideoGetStringTotalWidth(const char * szc) {
    unsigned int nWidth=0;
    bool fDouble=false;
    while(*szc) {
        if(*szc=='\2') {
            fDouble=!fDouble;
            szc++;
        } else {
            nWidth+=BootVideoGetCharacterWidth(*szc++, fDouble);
        }
    }
    return nWidth;
}



void BootVideoJpegBlitBlend(
    u8 *pDst,
    u32 dst_width,
    JPEG * pJpeg,
    u8 *pFront,
    RGBA m_rgbaTransparent,
    u8 *pBack,
    int x,
    int y
) {
    int n=0;

    int nTransAsByte=m_rgbaTransparent>>24;
    int nBackTransAsByte=255-nTransAsByte;
    u32 dw;

    m_rgbaTransparent|=0xff000000;
    m_rgbaTransparent&=0xffc0c0c0;

    while(y--) {

        for(n=0; n<x; n++) {

            dw = ((*((u32 *)pFront))|0xff000000)&0xffc0c0c0;

            if(dw!=m_rgbaTransparent) {
                pDst[2]=((pFront[0]*nTransAsByte)+(pBack[0]*nBackTransAsByte))>>8;
                pDst[1]=((pFront[1]*nTransAsByte)+(pBack[1]*nBackTransAsByte))>>8;
                pDst[0]=((pFront[2]*nTransAsByte)+(pBack[2]*nBackTransAsByte))>>8;
            }
            pDst+=4;
            pFront+=pJpeg->bpp;
            pBack+=pJpeg->bpp;
        }
        pBack+=(pJpeg->width*pJpeg->bpp) -(x * pJpeg->bpp);
        pDst+=(dst_width * 4) - (x * 4);
        pFront+=(pJpeg->width*pJpeg->bpp) -(x * pJpeg->bpp);
    }
}

// usable for direct write or for prebuffered write
// returns width of character in pixels
// RGBA .. full-on RED is opaque --> 0xFF0000FF <-- red

int BootVideoOverlayCharacter(
    u32 * pdwaTopLeftDestination,
    u32 m_dwCountBytesPerLineDestination,
    RGBA rgbaColourAndOpaqueness,
    u8 bCharacter,
    bool fDouble
) {
    u8* pbaDestStart;

    if(bCharacter=='\t') {
        u32 dw=((u32)pdwaTopLeftDestination) % m_dwCountBytesPerLineDestination;
        u32 dw1=((dw+1)%(32<<2));  // distance from previous boundary
        return ((32<<2)-dw1)>>2;
    }

    u8 scale;
    (fDouble) ? (scale = 2) : (scale = 1);
    u8 height = 8 * scale;
    u8 width  = 8 * scale;


    if(bCharacter<'!') return width;
    if(bCharacter>'~') return width;

    pbaDestStart=((u8 *)pdwaTopLeftDestination);
    for(int y = 0; y < height; y++) {
        u8* pbaDest=pbaDestStart;
        u8 b = font8x8_basic[bCharacter][y/scale];
        for(int i = 0; i < width; i++) {
            if ((b >> (i/scale)) & 0x01) {
                pbaDest[0] = (rgbaColourAndOpaqueness>>0)  & 0xFF;
                pbaDest[1] = (rgbaColourAndOpaqueness>>8)  & 0xFF;
                pbaDest[2] = (rgbaColourAndOpaqueness>>16) & 0xFF;
                pbaDest[3] = (rgbaColourAndOpaqueness>>24) & 0xFF;
            }
            pbaDest+=4;
        }
        pbaDestStart+=m_dwCountBytesPerLineDestination;
    }
    return width;
}

// usable for direct write or for prebuffered write
// returns width of string in pixels

int BootVideoOverlayString(u32 * pdwaTopLeftDestination, u32 m_dwCountBytesPerLineDestination, RGBA rgbaOpaqueness, const char * szString) {
    unsigned int uiWidth=0;
    bool fDouble=0;
    while((*szString != 0) && (*szString != '\n')) {
        if(*szString=='\2') {
            fDouble=!fDouble;
        } else {
            uiWidth+=BootVideoOverlayCharacter(
                         pdwaTopLeftDestination+uiWidth, m_dwCountBytesPerLineDestination, rgbaOpaqueness, *szString, fDouble
                     );
        }
        szString++;
    }
    return uiWidth;
}

bool BootVideoJpegUnpackAsRgb(u8 *pbaJpegFileImage, JPEG * pJpeg) {

    struct jpeg_decdata *decdata;
    int size, width, height, depth;

    decdata = (struct jpeg_decdata *)malloc(sizeof(struct jpeg_decdata));
    memset(decdata, 0x0, sizeof(struct jpeg_decdata));

    jpeg_get_size(pbaJpegFileImage, &width, &height, &depth);
    size = ((width + 15) & ~15) * ((height + 15) & ~15) * (depth >> 3);

    pJpeg->pData = (unsigned char *)malloc(size);
    memset(pJpeg->pData, 0x0, size);

    pJpeg->width = ((width + 15) & ~15);
    pJpeg->height = ((height +15) & ~ 15);
    pJpeg->bpp = depth >> 3;

    if((jpeg_decode(pbaJpegFileImage, pJpeg->pData,
                    ((width + 15) & ~15), ((height + 15) & ~15), depth, decdata)) != 0) {
        printk("Error decode picture\n");
        // We dont really want this to lockup - those poor TSOPers!
        //while(1);
    }

    pJpeg->pBackdrop = BootVideoGetPointerToEffectiveJpegTopLeft(pJpeg);
    /*
    BootVideoJpegBlitBlend(
    	(u8 *)FB_START,
    	640,
    	pJpeg,
    	pJpeg->pData,
    	0,
    	pJpeg->pData,
    	pJpeg->width,
    	pJpeg->height
    ); while(1);
    */

    free(decdata);

    return false;
}

u8 * BootVideoGetPointerToEffectiveJpegTopLeft(JPEG * pJpeg) {
    return ((u8 *)(pJpeg->pData + pJpeg->width * ICON_HEIGHT * pJpeg->bpp));
}

void BootVideoClearScreen(JPEG *pJpeg, int nStartLine, int nEndLine) {
    VIDEO_CURSOR_POSX=vmode.xmargin;
    VIDEO_CURSOR_POSY=vmode.ymargin;

    if(nEndLine>=vmode.height) nEndLine=vmode.height-1;

    {
        if(pJpeg->pData!=NULL) {
            volatile u32 *pdw=((u32 *)FB_START)+vmode.width*nStartLine;
            int n1=pJpeg->bpp * pJpeg->width * nStartLine;
            u8 *pbJpegBitmapAdjustedDatum=pJpeg->pBackdrop;

            while(nStartLine++<nEndLine) {
                int n;
                for(n=0; n<vmode.width; n++) {
                    pdw[n]=0xff000000|
                           ((pbJpegBitmapAdjustedDatum[n1+2]))|
                           ((pbJpegBitmapAdjustedDatum[n1+1])<<8)|
                           ((pbJpegBitmapAdjustedDatum[n1])<<16)
                           ;
                    n1+=pJpeg->bpp;
                }
                n1+=pJpeg->bpp * (pJpeg->width - vmode.width);
                pdw+=vmode.width; // adding u32 footprints
            }
        }
    }
}

int VideoDumpAddressAndData(u32 dwAds, const u8 * baData, u32 dwCountBytesUsable) { // returns bytes used
    int nCountUsed=0;
    while(dwCountBytesUsable) {

        u32 dw=(dwAds & 0xfffffff0);
        char szAscii[17];
        char sz[256];
        int n=sprintf(sz, "%08X: ", dw);
        int nBytes=0;

        szAscii[16]='\0';
        while(nBytes<16) {
            if((dw<dwAds) || (dwCountBytesUsable==0)) {
                n+=sprintf(&sz[n], "   ");
                szAscii[nBytes]=' ';
            } else {
                u8 b=*baData++;
                n+=sprintf(&sz[n], "%02X ", b);
                if((b<32) || (b>126)) szAscii[nBytes]='.';
                else szAscii[nBytes]=b;
                nCountUsed++;
                dwCountBytesUsable--;
            }
            nBytes++;
            if(nBytes==8) n+=sprintf(&sz[n], ": ");
            dw++;
        }
        n+=sprintf(&sz[n], "   ");
        n+=sprintf(&sz[n], "%s", szAscii);
        sz[n++]='\n';
        sz[n++]='\0';

        printk(sz, n);

        dwAds=dw;
    }
    return 1;
}
void BootVideoChunkedPrint(const char * szBuffer) {
    int n=0;
    int nDone=0;

    while (szBuffer[n] != 0) {
        if(szBuffer[n]=='\n') {
            BootVideoOverlayString(
                (u32 *)((FB_START) + VIDEO_CURSOR_POSY * (vmode.width*4) + VIDEO_CURSOR_POSX),
                vmode.width*4, VIDEO_ATTR, &szBuffer[nDone]
            );
            nDone=n+1;
            VIDEO_CURSOR_POSY+=8;
            VIDEO_CURSOR_POSX=vmode.xmargin<<2;
        }
        n++;
    }
    if (n != nDone) {
        VIDEO_CURSOR_POSX+=BootVideoOverlayString(
                               (u32 *)((FB_START) + VIDEO_CURSOR_POSY * (vmode.width*4) + VIDEO_CURSOR_POSX),
                               vmode.width*4, VIDEO_ATTR, &szBuffer[nDone]
                           )<<2;
        if (VIDEO_CURSOR_POSX > (vmode.width -
                                 vmode.xmargin) <<2) {
            VIDEO_CURSOR_POSY+=8;
            VIDEO_CURSOR_POSX=vmode.xmargin<<2;
        }

    }

}

int printk(const char *szFormat, ...) {  // printk displays to video
    char szBuffer[512*2];
    u16 wLength=0;
    va_list argList;
    va_start(argList, szFormat);
    wLength=(u16) vsprintf(szBuffer, szFormat, argList);
//	wLength=strlen(szFormat); // temp!
//	memcpy(szBuffer, szFormat, wLength);
    va_end(argList);

    szBuffer[sizeof(szBuffer)-1]=0;
    if (wLength>(sizeof(szBuffer)-1)) wLength = sizeof(szBuffer)-1;
    szBuffer[wLength]='\0';

    BootVideoChunkedPrint(szBuffer);
    return wLength;
}

int console_putchar(int c) {
    char buf[2];
    buf[0] = (char)c;
    buf[1] = 0;
    BootVideoChunkedPrint(buf);
    return (int)buf[0];
}

//Fix for BSD
#ifdef putchar
#undef putchar
#endif
int putchar(int c) {
    return console_putchar(c);
}
