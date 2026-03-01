#include "doomzl_modern_video_drivers.h"

void test_iostream_usage()
{
    std::cout << "Call me mr. rao" << std::endl;
}

extern "C" void do_some_stuff()
{
    test_iostream_usage();
}