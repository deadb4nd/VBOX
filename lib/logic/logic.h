#include <stdbool.h>
#ifndef _LOGIC_H_
#define _LOGIC_H_

void spin_motor(int second);
void stop_spinning(int second);
void buzz(int time);
bool has_ball_moved(int ball_value, int last_value);
void update(int *selection);

#endif
