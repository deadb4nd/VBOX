#include "logic.h"
#include <stdbool.h>

bool has_ball_moved(int ball_value, int last_value) {
    if (ball_value == 1 && last_value == 0) {
        return true;
    } else {
        return false;
    }
}

void update(int *selection) {
    if (*selection >= 3) {
        *selection = 0;
    } else {
        *selection += 1;
    }

    return;
}
