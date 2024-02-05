// libraries.h
#ifndef LIBRARIES_H
#define LIBRARIES_H

#define CTRL_KEY(k) ((k) & 0x1f) // Macro to handle Ctrl key combinations

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#define KILO_VERSION "0.0.1"
#define KILO_TAB_STOP 8
#define KILO_QUIT_TIMES 3
#define HL_HIGHLIGHT_NUMBERS (1<<0)
#define HL_HIGHLIGHT_STRINGS (1<<1)


#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0])) //filetype Hightlight Database

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#endif 
