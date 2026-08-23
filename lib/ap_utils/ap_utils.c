#include <ap_utils.h>
#include <string.h>

// this only works if array is NULL terminated
int count_ssids(char *array[]) {
    int len = 0;
    while (array[len] != NULL) {
        len++;
    }
    return len;
}
