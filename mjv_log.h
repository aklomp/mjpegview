#ifndef MJV_LOG_H
#define MJV_LOG_H

#define _GNU_SOURCE  1  /* For vfprintf */

void log_info (const char *const fmt, ...);
void log_error (const char *const fmt, ...);
void log_debug (const char *const fmt, ...);

void log_debug_on (void);
void log_debug_off (void);

#endif	/* MJV_LOG_H */
