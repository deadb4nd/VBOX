#ifndef _AP_UTILS_H_
#define _AP_UTILS_H_

#define arr_len(array) ((int)(sizeof(array) / sizeof((array)[0])))

int const_arr_len(const char *array[]);

#endif
