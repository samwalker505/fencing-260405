#pragma once

#include <stdbool.h>

/** One poll: edge-detected buttons + fencing line (for LED + hits). */
typedef struct {
  bool go_press;
  bool next_press;
  bool fencing_rising;
  int fencing_level;
} app_input_t;
