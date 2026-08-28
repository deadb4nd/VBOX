#ifndef LOGIC_H
#define LOGIC_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief  Call this every loop iteration with the fresh GPIO level.
 * @return true once per physical roll (debounced).
 */
bool has_ball_moved(int ball_value, int last_value);

/**
 * @brief Increment selection, wrap at ACTION_COUNT.
 */
void update(int *selection);

#endif