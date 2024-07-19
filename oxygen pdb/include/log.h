#pragma once
#include <fltKernel.h>

#if DBG
#define printk(...)do{DbgPrintEx(77,0,__VA_ARGS__);}while(0);
#else
#define printk(...)do{}while(0);
#endif