/*
 * debug.cpp: --trace X Y (debug.h). Complete; nothing to change. The
 * traced pixel comes from the options cmdline.cpp filled; the flag is per
 * thread, so only the worker on that pixel prints.
 */
#include "debug.h"
#include "options.h"

#include <cstdarg>
#include <cstdio>

thread_local bool traceOn = false;

void traceBegin(int px, int py)
{
    traceOn = options.traceX >= 0 && px == options.traceX && py == options.traceY;
}

void traceEnd() { traceOn = false; }

void traceLine(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    std::vfprintf(stderr, format, args);
    va_end(args);
    std::fputc('\n', stderr);
}
