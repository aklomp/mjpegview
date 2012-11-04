#include <stdio.h>
#include <stdarg.h>

static int debug_on = 0;

void
log_info (const char *const fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vfprintf(stdout, fmt, args);
	va_end(args);
}

void
log_error (const char *const fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
}

void
log_debug (const char *const fmt, ...)
{
	va_list args;

	if (debug_on == 0) {
		return;
	}
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
}

void
log_debug_on (void)
{
	debug_on = 1;
}

void
log_debug_off (void)
{
	debug_on = 0;
}
