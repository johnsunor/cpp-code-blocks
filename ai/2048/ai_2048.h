/************************************
 *FileName:     ai_2048.h
 *Author:       johnsun
 *Date:         2014/10/12
 *Version:      4.0
 ************************************/

#ifndef AI2048_H
#define AI2048_H

#include "ai_utils.h"

namespace ai_set {

class AI2048 {
 public:
   typedef ::std::pair<int,int> PosPair;

   AI2048() {
     Clear();
   }

   void CopyTilesFrom(const AI2048& ai) {
     if (this != &ai) {
       memcpy(tiles_, ai.tiles_, sizeof(tiles_));
     }
   }

   void AddComputerMove(int grid[4][4]);
   
   int GetPlayerBestMove();

   void Print(const char* msg=NULL) const;
   void Clear();

   void GetAllTiles(int grid[4][4]);

   int GetAllEmptyPos(int pos_set[]) const;

   void AddRandTile();

   int GetCandidateSet(int candidate_pos_set[],
                       int candidate_val_set[]);

   bool IsValid(int pos) const;

   void Mark(int pos, int val, bool marked[20]) const;
   
   PosPair GetTargetPos(int pos, int dir) const; 

   int EvalSmoothnessValue() const;

   int GetMaxValue() const;

   int GetLandNum() const;

   int GetEmptyPosNum() const;

   int  EvalMonotonicityValue() const;

   double EvalAll() const;
  
   bool IsWin() const;

   bool TryMove(int dir);
   
   double MiniMaxSearch(int depth, bool is_player, double alpha, double beta);

 private:
   AI2048(const AI2048&);
   AI2048& operator=(const AI2048&);

 private:
   int8_t tiles_[20];
   int best_move_;
};

} //namespace ai_set

#endif //AI2048_H
