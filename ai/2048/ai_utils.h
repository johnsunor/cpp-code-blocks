#ifndef MY_UTILS_H
#define MY_UTILS_H

// C89
#include <cstdio>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <cassert>
#include <ctime>
#include <cassert>

// C99
#include <stdint.h>

// C++98
#include <iostream>
#include <string>
#include <vector>
#include <list>
#include <map>
#include <set>
#include <queue>
#include <stack>
#include <algorithm>
#include <utility>

// POSIX
#include <unistd.h>

// MACRO
#define INF 1000

namespace ai_utils {

const int offset[] = {
    -4,  // UP
    1,   // RIGHT
    4,   // DOWN
    -1   // LEFT
};

const int kUp = 0;
const int kRight = 1;
const int kDown = 2;
const int kLeft = 3;

const int dir_pos[16][4] = {
    {-1, 1, 4, -1},
    {-1, 2, 5, 0},
    {-1, 3, 6, 1},
    {-1, -1, 7, 2},
    {0, 5, 8, -1},
    {1, 6, 9, 4},
    {2, 7, 10, 5},
    {3, -1, 11, 6},
    {4, 9, 12, -1},
    {5, 10, 13, 8},
    {6, 11, 14, 9},
    {7, -1, 15, 10},
    {8, 13, -1, -1},
    {9, 14, -1, 12},
    {10, 15, -1, 13},
    {11, -1, -1, 14},
};

const double kSmoothWeight = 0.1;
const double kMonoWeight = 1.0;
const double kEmptyWeight = 2.7;
const double kMaxValueWeight = 1.0;

inline int MkPos(int x, int y) { return x * 4 + y; }

inline int X(int pos) { return pos / 4; }

inline int Y(int pos) { return pos % 4; }

inline ::std::size_t lg2(::std::size_t n) {
  if (n == 0) {
    printf("%u\n", static_cast<unsigned>(n));
    return 0;
  }
  assert(n > 0);
  ::std::size_t k = 0;
  for (; n > 1; n >>= 1) ++k;
  return k;
}

inline bool TimeOut(clock_t start_time, ::std::size_t msec) {
  return (clock() - start_time >
          static_cast<double>(msec) / 1000.0f * CLOCKS_PER_SEC);
}

template <typename T>
inline T ArrayMax(T array[], ::std::size_t array_size) {
  assert(array_size > 0);
  T max_val = array[0];
  for (::std::size_t i = 1; i < array_size; ++i) {
    if (max_val < array[i]) {
      max_val = array[i];
    }
  }
  return max_val;
}

}  // namespace ai_utils

#endif  // MY_UTILS_H
