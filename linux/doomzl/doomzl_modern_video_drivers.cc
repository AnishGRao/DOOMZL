
#include <stdlib.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <X11/extensions/XShm.h>

#ifdef LINUX
int XShmGetEventBase( Display* dpy );
#endif

#include <stdarg.h>
#include <stdint.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <memory>

#include <netinet/in.h>

#include "doomzl_modern_video_drivers.h"
#include <iostream>
#include <array>
#include <bitset>

namespace doomzl
{
    struct ColorConstants
    {
        ColorConstants() = default;
        ColorConstants(ColorConstants const &) = default;

        int red_shift = 0;
        int red_bits = 0;

        int blue_shift = 0;
        int blue_bits = 0;
        
        int green_shift = 0;
        int green_bits = 0;

        std::array<unsigned long, 256> colormap;
    };

    auto constants()
    {
        static std::shared_ptr<ColorConstants> constant = std::make_shared<ColorConstants>();
        return constant;
    }

    namespace BitTwiddle
    {
        [[nodiscard]]
        std::pair<int, int>
        get_truecolor_bits_and_shift(unsigned long const mask) noexcept
        {
            // TrueColor colormasks are expected to always be in contiguous formats, 
            // for their set bits. E.g. 0xF800->11111000000...
            // So, we will use this functionality to do our counts.
            using masktype = std::decay_t<decltype(mask)>;
            static constexpr size_t bitset_length_bytes = sizeof(masktype);
            // Byte is 8 bits
            static constexpr size_t bitset_length = bitset_length_bytes * 8;

            std::bitset<bitset_length> bitset(mask);

            // First we check the number of set bits
            // This is used to scale the 8-bit pseudocolor channel to 
            // the bitwidth found in set_bits
            int const set_bits = bitset.count();
            
            // Next we need to know the number of 0s starting from LSB
            // until the first set bit. This is used to shift the scaled
            // channel into place.
            int shift = 0;
            for (int i = 0; i<bitset_length; i++)
            {
                // If it is a set bit, break out.
                if (bitset.test(i))
                {
                    break;
                }
                // If it is not a set bit, increment the number of 0s
                // contiguous from LSB
                shift++;
            }

            return {set_bits, shift};
        }

        [[nodiscard]]
        static constexpr uint64_t truncate_or_expand_num_to_bitwidth(uint64_t const number, uint64_t const final_bitwidth, uint64_t const starting_bitwidth) noexcept
        {
            // The inputs are of type uint64_t, because those are guaranteed to hold maximal TrueColor widths.
            if (final_bitwidth > starting_bitwidth)
            {
                return number << ( final_bitwidth - starting_bitwidth );
            }

            return number >> ( starting_bitwidth - final_bitwidth );
        }
    }

    namespace Video
    {
        [[nodiscard]]
        int TrueColorFallback(Display*display, int const screen, XVisualInfo*visual_info)
        {
            // Grab the true depth
            int const depth = DefaultDepth(display, screen);

            // First check if XMatchInfo succeeds
            auto const match_success = XMatchVisualInfo(display, screen, depth, TrueColor, visual_info);
            if (!match_success)
            {
                return 0;
            }
            
            std::cout << "Falling back to TrueColor visual (depth "<< depth <<")" << std::endl; 

            auto [red_bits, red_shift] = BitTwiddle::get_truecolor_bits_and_shift(visual_info->red_mask);
            auto [green_bits, green_shift] = BitTwiddle::get_truecolor_bits_and_shift(visual_info->green_mask);
            auto [blue_bits, blue_shift] = BitTwiddle::get_truecolor_bits_and_shift(visual_info->blue_mask);
            
            constants()->red_shift = red_shift;
            constants()->red_bits = red_bits;
            constants()->green_bits = green_bits;
            constants()->green_shift = green_shift;
            constants()->blue_bits = blue_bits;
            constants()->blue_shift = blue_shift;

            return 1;
        }

        [[nodiscard]]
        unsigned long MapDoomColorToTrueColor(int const red, int const green, int const blue)
        {

            // Lots of casting in this function, so this will help readability
            using ulong = unsigned long;

            // Essentially we are taking an RGB value that used to just pass through the 8-bit pseudocolor,
            // but now we actually have TrueColor, so we use a single 8byte value to represent our TrueColor
            // pixel, with the shifts we stored above in ::TrueColorFallback(...) shoving our r/g/b into 
            // their expected places.
            // Think of it like having a char pixel[64] where red_shift is telling us the pixel[red_start:red_end]
            // and so on for the other colors. The other strange shifts are just truncating/expanding to 8bits,
            // as doom is 8bit, but TrueColor visuals could be 5bit, 6bit, 10bit, etc. 

            ulong red_pixel = BitTwiddle::truncate_or_expand_num_to_bitwidth(red, constants()->red_bits, 8);
            ulong green_pixel = BitTwiddle::truncate_or_expand_num_to_bitwidth(green, constants()->green_bits, 8);
            ulong blue_pixel = BitTwiddle::truncate_or_expand_num_to_bitwidth(blue, constants()->blue_bits, 8);


            // Shift them into where they need to be based on X11 True Color responses
            red_pixel <<= constants()->red_shift;
            green_pixel <<= constants()->green_shift;
            blue_pixel <<= constants()->blue_shift;

            // smoosh them together
            return red_pixel | green_pixel | blue_pixel;  
        }

        [[nodiscard]]
        int InitializeColorLUTFromGammaTable(int const usegamma, unsigned char *palette, unsigned char (*gammatable)[256])
        {
            // The gammatable is a byte[5][256] array initialized in v_video.c, and is just plain
            // magic numbers. No clue what its about, but I took logic from i_video.c and re-did it
            // with the different mappings.
            for (int i = 0; i < 256; i++)
            {
                constants()->colormap[i] = MapDoomColorToTrueColor(
                    gammatable[usegamma][*(palette + 3*i)],
                    gammatable[usegamma][*(palette + 3*i + 1)],
                    gammatable[usegamma][*(palette + 3*i + 2)]
                );
            }
            return 256*3;
        }

        void DoomFrameBufferToX11Image(XImage * image, int const height, int const width, int const multiply, int const doom_screenwidth, int const doom_screenheight, unsigned char ** screens)
        {
            for (int row = 0; row < height; row++)
            {
                int scaled_row_to_doom_row = (row * doom_screenheight) / height;
                unsigned char * scanline = (unsigned char * )(screens[0] + scaled_row_to_doom_row * doom_screenwidth);

                for (int col = 0; col < width; col++)
                {
                    int scaled_col_to_doom_col = (col * doom_screenwidth) / width;
                    XPutPixel(image, col, row, constants()->colormap[scanline[scaled_col_to_doom_col]]);
                }
            }
        }
    }
}

extern "C" void doomzl_DoomFrameBufferToX11Image(XImage * image, int height, int width, int multiply, int doom_screenwidth, int doom_screenheight, unsigned char ** screens)
{
    return doomzl::Video::DoomFrameBufferToX11Image(image, height, width, multiply, doom_screenwidth, doom_screenheight, screens);
}

extern "C" int doomzl_InitializeColorLUTFromGammaTable(int usegamma, unsigned char * palette,unsigned char (*gammatable)[256])
{
    return doomzl::Video::InitializeColorLUTFromGammaTable(usegamma, palette, gammatable);
}

extern "C" int doomzl_TrueColorFallback(Display*display, int screen, XVisualInfo*visual_info)
{
    return doomzl::Video::TrueColorFallback(display, screen, visual_info);
}

extern "C" unsigned long doomzl_MapDoomColorToTrueColor(int r, int g, int b)
{
    return doomzl::Video::MapDoomColorToTrueColor(r,g,b);
}