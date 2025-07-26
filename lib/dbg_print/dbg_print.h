#pragma once
#ifndef DBG_PRINT_H
#define DBG_PRINT_H


#ifdef DEBUG
#define dbg_print(...) Serial.print(__VA_ARGS__)
#define dbg_printf(...) Serial.printf(__VA_ARGS__)
#define dbg_println(...) Serial.println(__VA_ARGS__)
#else
#define dbg_print(...)
#define dbg_printf(...)
#define dbg_println(...)
#define info_print(...) print(__VA_ARGS__)
#define info_printf(...) printf(__VA_ARGS__)
#define info_println(...) println(__VA_ARGS__)
#endif

#endif // DBG_PRINT_H