#pragma once
#ifndef DBG_PRINT_H
#define DBG_PRINT_H


#ifdef DEBUG
#define dbg_print(...) dbg_print(__VA_ARGS__)
#define dbg_printf(...) dbg_printf(__VA_ARGS__)
#define dbg_println(...) dbg_println(__VA_ARGS__)
#else
#define dbg_print(...)
#define dbg_printf(...)
#define dbg_println(...)
#define info_print(...) dbg_print(__VA_ARGS__)
#define info_printf(...) dbg_printf(__VA_ARGS__)
#define info_println(...) dbg_println(__VA_ARGS__)
#endif

#endif // DBG_PRINT_H