#include <ap_utils.h>
#include <string.h>

// this only works if array is NULL terminated
int const_arr_len(const char *array[]) {
    int len = 0;
    while (array[len] != NULL) {
        len++;
    }
    return len;
}
