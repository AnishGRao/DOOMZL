/**
 * Written by Anish G. Rao
 *
 * This is likely crap. I'm not a gamedev, nor am I extraordinarily familiar with
 * code from this timespan. This is simply my effort to get this working on my own,
 * fueled by nothing but caffeine and nicotine.
 */

#pragma once

// Here we are essentially just showing C the calls it can make into us. 
// All other code will be C++ except stuff in this extern (and the corrollary
// function definitions in C)
#ifdef __cplusplus
extern "C" {
#endif
    int doomzl_TrueColorFallback(Display*display, int screen, XVisualInfo*visual_info);
    unsigned long doomzl_MapDoomColorToTrueColor(int r, int g, int b);
    int doomzl_InitializeColorLUTFromGammaTable(int usegamma, unsigned char * palette, unsigned char (*gammatable)[256]);
    void doomzl_DoomFrameBufferToX11Image(XImage * image, int height, int width, int multiply, int doom_screenwidth, unsigned char ** screens);

#ifdef __cplusplus
}
#endif
