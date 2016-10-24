#include "ai_2048.h"

namespace ai_set {

void AI2048::Print(const char* msg) const {
  if (msg != NULL) puts(msg);

  puts("<<<<<<<<<<<<<<<<<<<<<");
  for (int i = 0; i < 16; ++i) {
    if (tiles_[i] > 0) {
      printf("%-8d", 1 << tiles_[i]);
    } else {
      printf("%-8d", 0);
    }
    if (i % 4 == 3) {
      printf("\n");
    }
  }
  puts(">>>>>>>>>>>>>>>>>>>>>\n");
}

void AI2048::AddComputerMove(int grid[4][4]) {
  for (int i = 0; i < 16; ++i) {
    if (tiles_[i] == 0 && grid[i / 4][i % 4] > 0) {
      tiles_[i] = static_cast<int8_t>(::ai_utils::lg2(grid[i / 4][i % 4]));
    }
  }
}

void AI2048::GetAllTiles(int grid[4][4]) {
  for (int i = 0; i < 16; ++i) {
    if (tiles_[i] > 0) {
      grid[i / 4][i % 4] = static_cast<int>(1 << tiles_[i]);
    } else {
      grid[i / 4][i % 4] = 0;
    }
  }
}

inline int AI2048::GetAllEmptyPos(int pos_set[]) const {
  int set_size = 0;
  for (int i = 0; i < 16; ++i) {
    if (tiles_[i] == 0) {
      pos_set[set_size++] = i;
    }
  }
  return set_size;
}

void AI2048::AddRandTile() {
  int pos_set[16];
  int size = GetAllEmptyPos(pos_set);
  if (size > 0) {
    int idx = rand() % size;
    tiles_[pos_set[idx]] = (rand() % 2) ? 1 : 2;
  }
}

void AI2048::Clear() {
  best_move_ = -1;
  memset(tiles_, 0, sizeof(tiles_));
}

inline bool AI2048::IsValid(int pos) const { return (pos >= 0 && pos <= 15); }

inline void AI2048::Mark(int pos, int val, bool marked[20]) const {
  if (IsValid(pos) && tiles_[pos] == val && !marked[pos]) {
    marked[pos] = true;
    for (int dir = 0; dir < 4; ++dir) {
      Mark(::ai_utils::dir_pos[pos][dir], val, marked);
    }
  }
}

inline int AI2048::GetLandNum() const {
  int land_num = 0;
  bool marked[20];

  memset(marked, false, sizeof(marked));
  for (int i = 0; i < 16; ++i) {
    if (tiles_[i] != 0 && !marked[i]) {
      ++land_num;
      Mark(i, tiles_[i], marked);
    }
  }
  return land_num;
}

inline int AI2048::GetMaxValue() const {
  int8_t max_val = tiles_[0];
  for (int i = 1; i < 16; ++i) {
    if (max_val < tiles_[i]) {
      max_val = tiles_[i];
    }
  }
  return max_val;
}

inline AI2048::PosPair AI2048::GetTargetPos(int pos, int dir) const {
  int prev_pos = pos;
  do {
    prev_pos = pos;
    pos = ::ai_utils::dir_pos[pos][dir];
  } while (IsValid(pos) && tiles_[pos] == 0);
  return ::std::make_pair(prev_pos, pos);
}

inline int AI2048::EvalSmoothnessValue() const {
  int smooth_val = 0;
  for (int i = 0; i < 16; ++i) {
    if (tiles_[i] > 0) {
      for (int dir = 1; dir <= 2; ++dir) {
        PosPair pos_pair = GetTargetPos(i, dir);
        int target_pos = pos_pair.second;
        if (IsValid(target_pos) && tiles_[target_pos] > 0) {
          smooth_val -= ::abs(tiles_[i] - tiles_[target_pos]);
        }
      }
    }
  }
  return smooth_val;
}

inline bool AI2048::IsWin() const {
  for (int i = 0; i < 16; ++i) {
    if (tiles_[i] == 11) {  // 2^11
      return true;
    }
  }
  return false;
}

inline int AI2048::EvalMonotonicityValue() const {
#define WORK(ids, val1, val2, add)                                  \
  do {                                                              \
    for (int i = 0; i < 4; ++i) {                                   \
      int curr = ids[i][0];                                         \
      int next = curr + add;                                        \
      while (next <= ids[i][1]) {                                   \
        while (next <= ids[i][1] && tiles_[next] == 0) next += add; \
        next = next > ids[i][1] ? ids[i][1] : next;                 \
        if (tiles_[curr] > tiles_[next]) {                          \
          val1 += tiles_[next] - tiles_[curr];                      \
        } else {                                                    \
          val2 += tiles_[curr] - tiles_[next];                      \
        }                                                           \
        curr = next;                                                \
        next += add;                                                \
      }                                                             \
    }                                                               \
  } while (0)

  // int ids[4][4] = {
  //    0,  1,  2,  3,
  //    4,  5,  6,  7,
  //    8,  9,  10, 11,
  //    12, 13, 14, 15
  //};

  //========================
  // ROW
  static const int ids[4][2] = {{0, 3},  // START -- END
                                {4, 7},
                                {8, 11},
                                {12, 15}};

  // COLUMN
  static const int ids2[4][2] = {{0, 12},  // START -- END
                                 {1, 13},
                                 {2, 14},
                                 {3, 15}};
  //========================

  int totals[4] = {0};
  WORK(ids, totals[0], totals[1], 1);
  WORK(ids2, totals[2], totals[3], 4);

  return ::std::max(totals[0], totals[1]) + ::std::max(totals[2], totals[3]);
}

inline int AI2048::GetEmptyPosNum() const {
  int count = 0;
  for (int i = 0; i < 16; ++i) {
    if (tiles_[i] == 0) {
      count++;
    }
  }
  return count;
}

inline double AI2048::EvalAll() const {
  using namespace ai_utils;
  return kSmoothWeight * EvalSmoothnessValue() +
         kMonoWeight * EvalMonotonicityValue() +
         kEmptyWeight * static_cast<double>(lg2(GetEmptyPosNum())) +
         kMaxValueWeight * GetMaxValue();
}

bool AI2048::TryMove(int dir) {
  // POS
  static const int pos_list[2][4] = {
      {0, 1, 2, 3},  //
      {3, 2, 1, 0}  // reverse
  };
  int x = dir == ::ai_utils::kDown ? 1 : 0;
  int y = dir == ::ai_utils::kRight ? 1 : 0;
  bool can_move = false;
  bool merged[20] = {false};

  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      int pos = pos_list[x][i] * 4 + pos_list[y][j];
      if (tiles_[pos] > 0) {
        PosPair pos_pair = GetTargetPos(pos, dir);
        int target_pos0 = pos_pair.first;
        int target_pos = pos_pair.second;
        if (IsValid(target_pos) && tiles_[target_pos] == tiles_[pos] &&
            !merged[target_pos]) {
          tiles_[target_pos]++;
          tiles_[pos] = 0;
          merged[target_pos] = true;
          can_move = true;
        } else if (pos != target_pos0) {
          tiles_[target_pos0] = tiles_[pos];
          tiles_[pos] = 0;
          can_move = true;
        }
      }
    }
  }
  return can_move;
}

int AI2048::GetCandidateSet(int candidate_pos_set[], int candidate_val_set[]) {
  static const int val_set[] = {1, 2};
  int score[2][20] = {{0}, {0}};
  int empty_pos[20] = {0};
  int empty_pos_size = 0;

  empty_pos_size = GetAllEmptyPos(empty_pos);
  for (int i = 0; i < 2; ++i) {
    for (int j = 0; j < empty_pos_size; ++j) {
      tiles_[empty_pos[j]] = static_cast<int8_t>(val_set[i]);
      score[i][j] = -EvalSmoothnessValue() + GetLandNum();
      tiles_[empty_pos[j]] = 0;
    }
  }

  int max_score = ::std::max(::ai_utils::ArrayMax(score[0], empty_pos_size),
                             ::ai_utils::ArrayMax(score[1], empty_pos_size));
  int candidate_set_size = 0;
  for (int i = 0; i < 2; ++i) {
    for (int j = 0; j < empty_pos_size; ++j) {
      if (score[i][j] == max_score) {
        candidate_pos_set[candidate_set_size] = empty_pos[j];
        candidate_val_set[candidate_set_size++] = val_set[i];
      }
    }
  }
  return candidate_set_size;
}

double AI2048::MiniMaxSearch(int depth, bool is_player, double alpha,
                             double beta) {
  if (depth <= 0 || IsWin()) return EvalAll();

  AI2048 new_ai;
  if (is_player) {
    best_move_ = -1;
    for (int dir = 0; dir < 4; ++dir) {
      new_ai.CopyTilesFrom(*this);
      if (!new_ai.TryMove(dir)) continue;
      double score = new_ai.MiniMaxSearch(depth - 1, !is_player, alpha, beta);
      if (score > alpha) {
        alpha = score;
        best_move_ = dir;
      }
      if (beta <= alpha) return beta;
    }
  } else {
    int candidate_pos_set[20] = {0};
    int candidate_val_set[20] = {0};
    int candidate_set_size = 0;
    candidate_set_size = GetCandidateSet(candidate_pos_set, candidate_val_set);
    for (int i = 0; i < candidate_set_size; ++i) {
      new_ai.CopyTilesFrom(*this);
      new_ai.tiles_[candidate_pos_set[i]] =
          static_cast<int8_t>(candidate_val_set[i]);
      double score = new_ai.MiniMaxSearch(depth, !is_player, alpha, beta);
      if (score < beta) beta = score;
      if (beta <= alpha) return alpha;
    }
  }
  return is_player ? alpha : beta;
}

int AI2048::GetPlayerBestMove() {
  int best_move = 0;
  int depth = 1;
  double best_score = -INF;
  clock_t start_time = clock();

  while (!::ai_utils::TimeOut(start_time, 100)) {
    double new_score = MiniMaxSearch(depth, true, -INF, INF);
    if (best_move_ == -1) break;
    if (new_score > best_score) {
      best_move = best_move_;
    }
    depth++;
  }

  return best_move;
}

}  // namespace ai_set
