//Chess4 chess.c by Steve Rhoads 2/24/09
#ifndef PLASMA
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#else
#include "dll.h"
#endif
#if defined(WIN32) && !defined(__TINYC__)
   #include <windows.h>
   #include <conio.h>
   #define GRAPHICS
   #define getch() _getch()
#else
   #define GetTickCount() 0
   #undef getch
   #define getch()
#endif
#ifndef min
   #define min(A,B) ((A)<(B)?(A):(B))
#endif
#ifndef isdigit
   #define isdigit(c) ('0'<=(c)&&(c)<='9')
#endif

#define BOARD_SIZE 256
#define DEPTH_MAX 20
#define MOVE_MAX 4000
#define TURNS_MAX 300
#define SRC_LIST_OFFSET 14*16
#define MAX_NEG -10000
#define MAX_NEG2 (-2000<<20)

enum {WHITE=1, BLACK=-1};
enum {SPACE=0, PAWN=1, KNIGHT=2, BISHOP=3, ROOK=4, QUEEN=5, KING=6, INVALID=64};
static const char piece_storage[]="kqrbnp-PNBRQK", *piece_name=piece_storage+6;
static const char piece_move_offset[] = {0, 0, 0, 10, 16, 22, 32};
static const signed char piece_moves[] = {
   /*knight*/ 0, -33, -31, -18, -14, +14, +18, +31, +33,   0,
   /*biship*/ 1, -17, -15, +15, +17,   0,
   /*rook*/   1, -16,  -1,  +1, +16,   0,
   /*queen*/  1, -17, -16, -15,  -1,  +1, +15, +16, +17,   0,
   /*king*/   0, -17, -16, -15,  -1,  +1, +15, +16, +17,   0
};

//Board evaluation parameters  
static const int piece_value[]={0,100,300,320,500,900,1500};
static const char board_hold[]={0,1,4,12,12,4,1,0}; //center of board is important
static const signed char board_hold2[]={0,5,5,5,5,5,5,0};  //knight & bishop
enum {
   VALUE_RANDOM = 12,
   VALUE_PAWN_MOVE = 2,
   VALUE_PAWN_MOVE2 = 4,
   VALUE_PAWN_MOVE_EARLY = 10,
   VALUE_PAWN_CHAIN = 10,
   VALUE_PAWN_ADVANCED = 20,
   VALUE_PAWN_DOUBLED = -20,
   VALUE_PAWN_PROTECT_KING = 40,
   VALUE_STOP_PROMOTION = 100,
   VALUE_QUEEN_EARLY = -20,
   VALUE_QUEEN_ATTACKED = -20,
   VALUE_KING_EARLY = -30,
   VALUE_KING_ATTACKED = -30,
   VALUE_CASTLE_LEFT = 40,
   VALUE_CASTLE_RIGHT = 60,
   VALUE_MOSTLY_BAD = -40,
   VALUE_AVOID_STALEMATE = 10,
   VALUE_AVOID_STALEMATE2 = 50,
   VALUE_AVOID_STALEMATE3 = 200,
   VALUE_CAPTURE_SHIFT = 5,
   VALUE_MOBILITY_SHIFT = 0
};

static signed char board_weight[BOARD_SIZE];
static signed char board_weight2[BOARD_SIZE];  //for knight and bishop
static const char board_init_pieces[]={
#if 1
   "rnbqkbnr"
   "pppppppp"
   "        "
   "        "
   "        "
   "        "
   "PPPPPPPP"
   "RNBQKBNR"
#endif
};

static int monitorSrcDst;
static int monitor[]={0, 0x6755, 0x1022, 0x4645, 0x3133, 0x5746, 0x6052, 0x4767, 0x0000};

//The board is a 16x16 board which uses a padding of two INVALID squares
//around the 8x8 grid.  The upper left square is at offset of two
//both horizontally and vertically so the offset is 34 = 16 + 16 + 2.
//At SRC_LIST_OFFSET is the 16 src position of BLACK pieces followed by 
//the 16 src positions of WHITE pieces.
static signed char board[256];
static signed char boardHit[256];  //what locations can be hit
static unsigned int undoStorage[DEPTH_MAX*8], *undoPtr = undoStorage;
static unsigned char highlight[256];

//12-bit signed value; 4-bit index; 8-bit src; 8-bit dst
#define MOVE_ADD(VALUE, INDEX, SRC, DST) *move++=((VALUE)<<20)|((INDEX)<<16)|((SRC)<<8)|(DST)
static int moveStorage[MOVE_MAX], *movePtr=moveStorage;
static int moveBest[DEPTH_MAX+2];
static int history[TURNS_MAX+DEPTH_MAX+5];
static unsigned int historyHash[TURNS_MAX+DEPTH_MAX+5];
static int historyCount;
static int stalemate;
static int kingMoved[2];
static int depthCurrent=0;
static unsigned int moveCount;
static int levelColor[2]={3,3};

#ifdef GRAPHICS
#include "chess.h"
#else
#define ChessUserGraphics(A) 1
#define ChessDrawInit()
#endif
signed char *ChessBoard(void) {return board;};
void ChessPieceChange(int pos) {if(++board[pos] > KING) board[pos] = -KING;}
void ChessLevel(int level) {levelColor[0] = level; moveCount = 0;}
unsigned char *ChessHighlight(void) {return highlight;}


//Setup the chess board
void ChessInit(const char *init_board)
{
   int x, y, piece;

   if(init_board == NULL)
      init_board = board_init_pieces;
   for(x = 0; x < BOARD_SIZE; ++x) 
      board[x] = INVALID;
   for(y = 0; y < 8; ++y) 
   {
      for(x = 0; x < 8; ++x) 
      {
         board[y * 16 + x + 34] = 0;
         for(piece = -KING; piece <= KING; ++piece) 
         {
            if(init_board[y * 8 + x] == piece_name[piece]) 
            {
               board[y * 16 + x + 34] = (signed char)piece;
               break;
            }
         }
         board_weight[y * 16 + x + 34] = (signed char)min(board_hold[x], board_hold[y]);
         board_weight2[y * 16 + x + 34] = (signed char)min(board_hold2[x], board_hold2[y]);
      }
   }
   kingMoved[0] = 0;
   kingMoved[1] = 0;
   historyCount = 0;
}


//Show the chess board
static char *ChessShow(char *buf)
{
   int x, y;
   int src=0, dst=0;
   signed char name, piece;
   char text[256];
   char *ptr;

   if(buf)
      ptr = buf;
   else
      ptr = text;
   memset(ptr, 0, 256);
   src = (moveBest[depthCurrent] >> 8) & 0xff;
   dst = moveBest[depthCurrent] & 0xff;
   if(moveBest[depthCurrent])
      sprintf(ptr, "\n%d%d%d%d", src%16-2, src/16-2, dst%16-2, dst/16-2);
   ptr += strlen(ptr);

   sprintf(ptr, "\n  0 1 2 3 4 5 6 7");
   ptr += strlen(ptr);
   for(y = 2; y < 10; ++y) 
   {
      sprintf(ptr, "\n%d ", y - 2);
      ptr += strlen(ptr);
      for(x = 2; x < 10; ++x) 
      {
         piece = board[y * 16 + x];
         name = piece_name[piece];
         if(name == '-' && ((x + y) & 1)) 
            name = '*';
         if(depthCurrent == 0 && src == y * 16 + x) 
            name = '+';
         *ptr++ = name;
         if(dst != y * 16 + x) 
            *ptr++ = ' ';
         else 
            *ptr++ = '.';
      }
   }
   *ptr++ = '\n';
   *ptr++ = 0;
   if(buf == NULL)
      printf("%s", text);
   return buf;
}


//Fill in boardHit
static int ChessMarkHit(int color, int *pHitCount)
{
   int piece;
   int repeat, delta, src, dst;
   int index;
   int bestPiece=0;
   int count=0;
   unsigned char *srcList;
   const signed char *deltaPtr;

   //Clear mark table
#if 1
   unsigned int *hit = (unsigned int*)boardHit;
   hit[ 8] = hit[ 9] = hit[10] = hit[12] = hit[13] = hit[14] = 0;
   hit[16] = hit[17] = hit[18] = hit[20] = hit[21] = hit[22] = 0;
   hit[24] = hit[25] = hit[26] = hit[28] = hit[29] = hit[30] = 0;
   hit[32] = hit[33] = hit[34] = hit[36] = hit[37] = hit[38] = 0;
#else
   memset(boardHit+32, 0, 8*16);
#endif

   srcList = (unsigned char*)board + SRC_LIST_OFFSET;
   if(color < 0)
      srcList += 16;

   //Loop through the 16 pieces on the board
   for(index = 0; index < 16; ++index)
   {
      src = srcList[index];
      if(src < 0x22)
         break;
      piece = board[src];
      if(piece < 0)
         piece = -piece;

      if(piece == PAWN) 
      {
         delta = 16;
         if(board[src] == PAWN)
            delta = -16;
         boardHit[src + delta - 1] = 1;
         boardHit[src + delta + 1] = 1;
         count += 2;
         continue;
      }

      //Determine the moves for the piece
      deltaPtr = piece_moves + piece_move_offset[piece];
      repeat = *deltaPtr++;
      delta = *deltaPtr++;
      do
      {
         dst = src;
         do 
         {
            dst += delta;
            boardHit[dst] = 1;
            ++count;
            if(board[dst])
            {
               piece = abs(board[dst]);
               if(piece <= KING && piece > bestPiece)
                  bestPiece = piece;
               break;
            }
         } while(repeat);
         delta = *deltaPtr++;
      } while(delta);
   }
   *pHitCount = count;
   return bestPiece;
}


//Calculate all possible moves from the srcList
static int ChessMovesFind(int color, int *moveList)
{
   int piece_from, piece, piece_to;
   int repeat, delta, src, dst;
   int index, y, valueStart, value, value2;
   int hitCount;
   unsigned char *srcList;
   const signed char *deltaPtr;
   int *move=moveList;

   if(depthCurrent <= 3)
      ChessMarkHit(color, &hitCount);

   srcList = (unsigned char*)board + SRC_LIST_OFFSET;
   if(color == WHITE)
      srcList += 16;

   //Loop through the 16 pieces on the board
   for(index = 0; index < 16; ++index)
   {
      src = srcList[index];
      if(src < 34)
         break;
      piece_from = board[src];
      piece = piece_from;
      if(piece < 0)
         piece = - piece;

      if(piece == PAWN) 
      {
         y = src >> 4;
         delta = 16;
         if(piece_from == PAWN)
            delta = -16;
         value = VALUE_PAWN_MOVE - board_weight[src];
         if(historyCount < 10)
            value += VALUE_PAWN_MOVE_EARLY;
         dst = src + delta;
         if(dst < 3*16 || dst > 9*16)
            value += piece_value[QUEEN] - piece_value[PAWN]*3; //become queen
         if(board[dst+delta] == INVALID || board[dst+delta+delta] == INVALID)
            value += VALUE_PAWN_ADVANCED;                    //advanced pawn
         if(src == 0x37 && board[0x28] == -KING)
            value -= VALUE_PAWN_PROTECT_KING;                //protect king
         if(src == 0x87 && board[0x98] == KING)
            value -= VALUE_PAWN_PROTECT_KING;                //protect king
         if(board[dst] == 0)
         {
            //Check for pawn chains
            value2 = 0;
            if(depthCurrent <= 4)
            {
               if(board[src-1] == piece_from || board[src+1] == piece_from)
                  value2 += VALUE_PAWN_CHAIN;
               if(board[dst+delta-1] == piece_from || board[dst+delta+1] == piece_from)
                  value2 += VALUE_PAWN_CHAIN;
               if(board[dst-1] == piece_from || board[dst+1] == piece_from)
                  value2 -= VALUE_PAWN_CHAIN;
               if(board[src-delta-1] == piece_from || board[src-delta+1] == piece_from)
                  value2 -= VALUE_PAWN_CHAIN;
            }
            MOVE_ADD(value + value2 + board_weight[dst], index, src, dst);

            //Check for two squares
            dst = src + delta + delta;
            if((y == 3 || y == 8) && board[dst] == 0)
               MOVE_ADD(value + VALUE_PAWN_MOVE2 + board_weight[dst], index, src, dst);
         }

         //Check if jump
         dst = src + delta - 1;
         if(board[dst] && board[dst] != INVALID && (board[dst] ^ piece_from) < 0)
         {
            //Check if double pawn
            value2 = value + board_weight[dst] + piece_value[abs(board[dst])];
            value2 += (board[dst+delta] == piece || board[dst-delta] == piece) ? 
                      VALUE_PAWN_DOUBLED : 0;
            MOVE_ADD(value2, index, src, dst);
         }
         dst = src + delta + 1;
         if(board[dst] && board[dst] != INVALID && (board[dst] ^ piece_from) < 0)
         {
            value2 = value + board_weight[dst] + piece_value[abs(board[dst])];
            value2 += (board[dst+delta] == piece || board[dst-delta] == piece) ? 
                      VALUE_PAWN_DOUBLED : 0;
            MOVE_ADD(value2, index, src, dst);
         }

         //Check if enpass
         if(board[src-1] == -piece_from)
         {
            value2 = history[historyCount + depthCurrent - 1] & 0xffff;
            if(value2 == ((src + delta + delta - 1) << 8 | (src - 1)))
               MOVE_ADD(piece_value[PAWN], index, src, src + delta - 1);
         }
         if(board[src+1] == -piece_from)
         {
            value2 = history[historyCount + depthCurrent - 1] & 0xffff;
            if(value2 == ((src + delta + delta + 1) << 8 | (src + 1)))
               MOVE_ADD(piece_value[PAWN], index, src, src + delta + 1);
         }
         continue;
      }
      else if(piece == KING)     //King Castle
      { 
         if((src == 0x26 || src == 0x96) && kingMoved[piece_from == KING] == 0)
         {
            if(abs(board[src-4]) == ROOK && board[src-3] == 0 && board[src-2] == 0 && 
               board[src-1] == 0 && (depthCurrent > 3 || (boardHit[src] == 0 && boardHit[src-1] == 0)))
               MOVE_ADD(VALUE_CASTLE_LEFT, index, src, src - 2);
            if(board[src+1] == 0 && board[src+2] == 0 && abs(board[src+3]) == ROOK &&
               (depthCurrent > 3 || (boardHit[src] == 0 && boardHit[src+1] == 0)))
               MOVE_ADD(VALUE_CASTLE_RIGHT, index, src, src + 2);
         }
      }

      //Determine the moves for the piece
      valueStart = -board_weight[src];
      if(piece == QUEEN)
         valueStart += historyCount < 14 ? VALUE_QUEEN_EARLY : -1;
      else if(piece == KING)
         valueStart += historyCount < 20 ? VALUE_KING_EARLY : -3;
      deltaPtr = piece_moves + piece_move_offset[piece];
      repeat = *deltaPtr++;
      delta = *deltaPtr++;
      do
      {
         dst = src;
         do 
         {
            dst += delta;
            piece_to = board[dst];
            if(piece_to == INVALID || (piece_to && (piece_to ^ piece_from) >= 0))
               break;
            if(piece_to < 0)
               piece_to = -piece_to;
            value = valueStart + piece_value[piece_to] + board_weight[dst];
            if(piece == KNIGHT || piece == BISHOP)
               value += board_weight2[dst] - board_weight2[src];
            if(piece_to == PAWN && ((board[dst] == PAWN && dst < 0x40) ||
                                    (board[dst] == -PAWN && dst > 0x80)))
               value += VALUE_STOP_PROMOTION;
            if(piece == KING && depthCurrent <= 3 && boardHit[dst])
               break;
            MOVE_ADD(value, index, src, dst);
            if(piece_to) 
               break;
         } while(repeat);
         delta = *deltaPtr++;
      } while(delta);
   }

   return (int)(move - moveList);
}


static int ChessHash(int srcDst, int *pValue)
{
   unsigned int *pBoard = (unsigned int*)board;
   unsigned int hash = 0;
   int i, piece, pieceAbs;
   int value = 0;
   int src, dst;
   signed char pieceSrc, pieceDst;

   src = (srcDst >> 8) & 0xff;
   dst = srcDst & 0xff;
   pieceSrc = board[src];
   pieceDst = board[dst];
   board[src] = 0;
   board[dst] = pieceSrc;

   for(i = 0x22; i <= 0x99; ++i)
   {
      piece = board[i];
      if(pieceSrc < 0)
         piece = -piece;
      pieceAbs = abs(piece);
      if(pieceAbs <= KING)
      {
         if(piece >= 0)
            value += piece_value[pieceAbs];
         else
            value -= piece_value[pieceAbs];
      }
   }
   *pValue = value;

   for(i = 8; i < 39; ++i)
      hash += (hash << 4) + (hash >> 23) + pBoard[i];

   board[src] = pieceSrc;
   board[dst] = pieceDst;
   return hash;
}


//Sort moves
static int ChessMovesSort(int count, int *moveList)
{
   int *listSort = moveList + count;
   int offset[] = {0, 128, 128*2};
   int index, value, value2, bin;
   int valueBest = MAX_NEG2;
   int valuePrev = MAX_NEG2;
   int srcDst;
   int srcDstPrev = moveBest[depthCurrent] & 0xffff;
   unsigned int hash;

   //Prevent stalemate
   if(depthCurrent == 0 || depthCurrent == 2)
   {
      for(index = 0; index < count; ++index)
      {
         for(bin = 0; bin < historyCount; ++bin)
         {
            if((history[bin] & 0xf00ff) == (moveList[index] & 0xf00ff))
            {
               hash = ChessHash(moveList[index], &value);
               if(hash == historyHash[bin] && value > -piece_value[BISHOP])
               {
                  if(value < 0)
                     value2 = VALUE_AVOID_STALEMATE;
                  else if(value <= piece_value[BISHOP])
                     value2 = VALUE_AVOID_STALEMATE2;
                  else
                     value2 = VALUE_AVOID_STALEMATE3;
                  moveList[index] -= value2 << 20;
               }
            }
         }
      }
   }

   for(index = 0; index < count; ++index)
   {
      if(depthCurrent == 0 && VALUE_RANDOM)
         moveList[index] += (rand() % VALUE_RANDOM) << 20;
      value = moveList[index];
      value2 = value >> 20;
      srcDst = value & 0xffff;
      if(srcDst == srcDstPrev)
      {
         valuePrev = value;
         continue;
      }
      if(value > valueBest)
         valueBest = value;
      if(value2 >= 50) 
         listSort[offset[0]++] = value;
      else if(value2 >= 0) 
         listSort[offset[1]++] = value;
      else
         listSort[offset[2]++] = value;
   }
   count = 0;
   if(valuePrev != MAX_NEG2)
      moveList[count++] = valuePrev;
   if(valueBest != MAX_NEG2)
      moveList[count++] = valueBest;
   for(bin = 0; bin < 3; ++bin)
   {
      for(index = bin * 128; index < offset[bin]; ++index)
      {
         if(listSort[index] != valueBest)
            moveList[count++] = listSort[index];
      }
   }
   //ChessMovesDump(count, moveSort);
   return count;
}


//Create a list of movable pieces and store into end of board
static void ChessMovesInit(void)
{
   int src, color;
   int index=0;
   unsigned char *srcList = (unsigned char*)board + SRC_LIST_OFFSET;

   memset(srcList, 0, 32);
   for(color = -1; color < 2; color += 2)
   {
      index = 0;
      for(src = 0; src < 12*16 && index < 16; ++src)
      {
         if(board[src] != INVALID && board[src] * color > 0)
            srcList[index++] = (unsigned char)src;
      }
      srcList += 16;
   }
   undoPtr = undoStorage;
   movePtr = moveStorage;
   moveCount = 0;
}


//Update srcList after move
static void ChessUpdate(int src, int dst)
{
   int i, offset;
   unsigned char *boardU = (unsigned char*)board;  //srcList

   offset = SRC_LIST_OFFSET;
   if(board[src] > 0)
      offset += 16;
   for(i = 0; i < 16; ++i)
   {
      if(boardU[offset + i] == src)
      {
         *undoPtr++ = src << 8 | (offset + i);
         boardU[offset + i] = (unsigned char)dst;
         break;
      }
   }
   if(i == 16)
      printf("ERROR4\n");
}


//Remove opponent's srcList item
static void ChessUpdateOpponent(int src, int dst)
{
   unsigned char *boardU = (unsigned char*)board; //srcList
   int i, offset, last;

   offset = SRC_LIST_OFFSET;
   if(board[src] < 0)
      offset += 16;
   for(i = 0; i < 16; ++i)
   {
      if(boardU[offset + i] == dst)
         break;
   }
   if(i == 16)
   {
      printf("ERROR2\n");
      return;
   }
   for(last = 15; last >= 0; --last)
   {
      if(boardU[offset + last])
         break;
   }
   *undoPtr++ = dst << 8 | (offset + i);
   boardU[offset + i] = 0;
   if(last > i)
   {
      *undoPtr++ = (unsigned int)board[offset + last] << 8 | (offset + last);
      boardU[offset + i] = boardU[offset + last];
      boardU[offset + last] = 0;
   }
}


//moveSrcDst = index << 16 | src << 8 | dst
static int ChessMovePiece(int moveSrcDst, int remember)
{
   int index, src, dst;
   int piece, diff;
   int offset;
   int rc=0;
   int i;
   unsigned char *boardU = (unsigned char*)board; //srcList
   (void)i;

   ++moveCount;
   src = (moveSrcDst >> 8) & 0xff;
   if(src < 0x22 || src > 0x99)
   {
      printf("Error %d\n", __LINE__);
      return 0;
   }
   dst = moveSrcDst & 0xff;
   history[historyCount + depthCurrent] = moveSrcDst;
   if(depthCurrent == 0 && remember)
   {
      historyHash[historyCount] = ChessHash(moveSrcDst, &offset);
      diff = 0;
      for(index = 0; index < historyCount; ++index)
      {
         if(historyHash[index] == historyHash[historyCount])
            ++diff;
      }
      if(diff > 1)
      {
         printf("StaleMate!\n");
         stalemate = 1;
      }
   }
   piece = board[src];
   if(piece == 0)
   {
      ChessShow(NULL);
      printf("ERROR1\n");
   }
   if(piece < 0)
      piece = -piece;

   //Update board undos
   *undoPtr++ = 0;
   *undoPtr++ = board[dst] << 8 | dst;
   *undoPtr++ = board[src] << 8 | src;

   //Update srcList
   offset = SRC_LIST_OFFSET;
   if(board[src] > 0)
      offset += 16;
   index = (moveSrcDst >> 16) & 0xf;
   *undoPtr++ = src << 8 | (offset + index);
   boardU[offset + index] = (unsigned char)dst;

   //Update opponents srcList
   if(board[dst])
   {
      if(abs(board[dst]) == KING)
         rc = -1;
      ChessUpdateOpponent(src, dst);
   }

   if(piece == PAWN)
   {
      if(dst < 3*16 || dst > 9*16)
         board[src] *= QUEEN;
      diff = (dst - src) & 0xf;
      if(board[dst] == SPACE && diff)      //enpass
      {
         if(diff != 1)
            diff = -1;
         ChessUpdateOpponent(src, src + diff);
         *undoPtr++ = board[src + diff] << 8 | (src + diff);
         board[src + diff] = SPACE;
      }
   }
   else if(piece == KING)                  //check KING Castle
   {
      rc = 1;
      diff = dst - src; 
      if(diff == -2) 
      {
         ChessUpdate(src-4, src-1);
         *undoPtr++ = board[src-1] << 8 | (src-1);
         *undoPtr++ = board[src-4] << 8 | (src-4);
         board[src-1] = board[src-4];
         board[src-4] = 0;
      }
      else if(diff == 2) 
      {
         ChessUpdate(src+3, src+1);
         *undoPtr++ = board[src+1] << 8 | (src+1);
         *undoPtr++ = board[src+3] << 8 | (src+3);
         board[src+1] = board[src+3];
         board[src+3] = 0;
      }
   }

   //Update the board
   board[dst] = board[src];
   board[src] = 0;

   return rc;
}


//Undo a recursive move
static void ChessUndo(void)
{
   while(*--undoPtr)
      board[*undoPtr & 0xff] = (signed char)(*undoPtr >> 8);
}


#ifdef DEBUG_CHESS
//Sanity check the srcLists
static void ChessCheck(void)
{
   unsigned char *srcList = (unsigned char*)board + SRC_LIST_OFFSET;
   int i;
   for(i = 0; i < 16; ++i)
   {
      if(srcList[i] && board[srcList[i]] >= 0)
      {
         ChessShow(NULL);
         printf("ERROR3\n");
      }
   }
   for(i = 16; i < 32; ++i)
   {
      if(srcList[i] && board[srcList[i]] <= 0)
      {
         ChessShow(NULL);
         printf("ERROR3\n");
      }
   }
}
#else
#define ChessCheck()
#endif


//Convert monitor to src|dst
static int Convert(int value)
{
   return ((value & 0xf000) >> 4) | ((value & 0x0f00) << 4) |
          ((value & 0x00f0) >> 4) | ((value & 0x000f) << 4);
}


//Recursively search the move space for the best move
//alpha = best found for me; beta = best found for opponent
static int ChessAlphaBeta(int color, int valueIn, int depth, int alpha, int beta)
{
   int src, dst;
   int index;
   int count;
   int value;
   int piece;
   int bestPiece;
   int goodCount, badCount;
   int monitorSrcDstSave;
   int hitCount;
   int *moveBase;
   int indexBest=0, valueBest=MAX_NEG;

   //Pre-search
   if(depth > 4*16)
   {
      monitorSrcDstSave = monitorSrcDst;
      monitorSrcDst = 0;
      if(depth > 5*16)
         ChessAlphaBeta(color, valueIn, 3*16, alpha, beta);
      else
         ChessAlphaBeta(color, valueIn, 2*16, alpha, beta);
      monitorSrcDst = monitorSrcDstSave;
   }

   //Calculate all possible moves
   count = ChessMovesFind(color, movePtr);

   //Determine if monitorSrcDst these moves
   if(monitorSrcDst)
      monitorSrcDst = Convert(monitor[depthCurrent] + 0x2222);
   
   //Avoid stalemate
   if(count == 0)
   {
      moveBest[depthCurrent] = 0x00;
      if(depthCurrent > 3)
         return 0;
      ChessMarkHit(color, &hitCount);
      value = KING * color;
      for(src = 0; src < 12*16; ++src)
      {
         if(board[src] == value)
         {
            if(boardHit[src] == 0)
               return 300;     //stalemate
            break;
         }
      }
      return -3000 + depthCurrent * 100;         //check mate
   }

   if(movePtr - moveStorage > MOVE_MAX - 512)
      depth = -160;

   //End of depth search
   if(depth <= 0)
   {
      //Fill in boardHit
      bestPiece = ChessMarkHit(color, &hitCount);
      
      for(index = 0; index < count; ++index)
      {
         value = movePtr[index] >> 20;
         value += (count - hitCount) >> VALUE_MOBILITY_SHIFT;  //value mobility
         src = (movePtr[index] >> 8) & 0xff;
         dst = movePtr[index] & 0xff;
         piece = abs(board[src]);
         if(boardHit[dst])
            value -= piece_value[piece];

         //Can queen or king be attacked
         if(bestPiece >= QUEEN && bestPiece > piece)
         {
            if(bestPiece != KING)
               value += VALUE_QUEEN_ATTACKED;
            else
               value += VALUE_KING_ATTACKED;
         }
         if(value > valueBest)
         {
            valueBest = value;
            indexBest = index;
         }
      }
      valueBest += valueIn;
      if(depth < 0)
         return valueBest;
      if(bestPiece < QUEEN && valueBest - valueIn < 50)
         return valueBest;
      if(valueBest <= alpha)
         return valueBest;   //already found a better move
      if(valueBest > beta)
         return valueBest;   //opponent won't let this happen

      //Double check that we can gain a piece
      moveBest[depthCurrent] = 0;
   }

   ChessMovesSort(count, movePtr);

   moveBase = movePtr;
   movePtr += count;
   goodCount = 0;
   badCount = 0;
   for(index = 0; index < count; ++index)
   {
      //Detect check
      dst = moveBase[index] & 0xff;
      if(abs(board[dst]) == KING)
      {
         valueBest = piece_value[KING] - 50 * depthCurrent;
         indexBest = index;
         break;
      }

      if(depth <= 0 && board[dst] == 0 && index)
      {
         //Only check if can capture piece for free
         if(valueBest < valueIn)
            valueBest = valueIn;
         break;
      }

      ChessCheck();
      ChessMovePiece(moveBase[index], 0);

      monitorSrcDstSave = monitorSrcDst;
      if(monitorSrcDst)
      {
         printf("%d:%4.4x:%d ", depthCurrent,
            Convert(moveBase[index] - 0x2222), moveBase[index] >> 20);
         if((moveBase[index] & 0xffff) == monitorSrcDst)
         {
            printf("\n");
            ChessShow(NULL);
         }
         else
            monitorSrcDst = 0;
      }

      //if(depthCurrent == 0)
      //   printf(".");
      ++depthCurrent;
      ChessCheck();
      value = valueIn + (moveBase[index] >> 20);
      value += value >> VALUE_CAPTURE_SHIFT;  //bird in hand better than in bush
      value = ChessAlphaBeta(-color, -value, depth - 16, -beta, -alpha);
      value = -value;
      if(value > valueBest)
      {
         valueBest = value;
         indexBest = index;
      }
      if(value - valueIn > 80)
         goodCount++;
      if(value - valueIn < -250)
         badCount++;
      --depthCurrent;
      ChessUndo();
      ChessCheck();

      monitorSrcDst = monitorSrcDstSave;
      if(monitorSrcDst)
      {
         printf("%d:%4.4x=%d ", depthCurrent,
            Convert(moveBase[index] - 0x2222), value);
      }

      //Alpha Beta pruning
      if(value > alpha)
         alpha = value;
      if(alpha >= beta)
         break;         //opponent won't let this happen
   }
   movePtr = moveBase;
   moveBest[depthCurrent] = movePtr[indexBest];

   //Detect trying to delay very bad outcome
   if(goodCount == 0 && badCount > 4 && count - goodCount - badCount < 4)
      valueBest += VALUE_MOSTLY_BAD;
   if(monitorSrcDst)
      printf("\n");

   return valueBest;
}


static int ChessUserCommand(int color, char *command)
{
   int x1, y1, x2, y2, src, dst;
   int count, index, level;

   if(isdigit(command[0]) && isdigit(command[1]) && 
      isdigit(command[2]) && isdigit(command[3])) 
   {
      x1 = command[0] - '0';
      y1 = command[1] - '0';
      x2 = command[2] - '0';
      y2 = command[3] - '0';
      src = y1 * 16 + x1 + 34;
      dst = y2 * 16 + x2 + 34;
      ChessMovesInit();
      count = ChessMovesFind(color, movePtr);
      if(count == 0)
         return 9;     //mate
      for(index = 0; index < count; ++index) 
      {
         if(((movePtr[index] >> 8) & 0xff) == src && 
            (movePtr[index] & 0xff) == dst) 
            break;
      }
      if(index == count) 
         return -1;                    //Illegal move
      moveBest[0] = movePtr[index]; 
      return 0;
   }
   if(command[0] == 'e') return 9;
   if(command[0] == 'g')
   {
      level = levelColor[0];
      levelColor[0] = levelColor[1];
      levelColor[1] = level;
      return 2;
   }
   if(command[0] == 'G') 
   {
      levelColor[1] = levelColor[0];
      return 2;
   }
   if(command[0] == 'c') 
   {
      for(y1 = 2; y1 < 10; ++y1) 
      {
         for(x1 = 2; x1 < 10; ++x1) 
            board[y1 * 16 + x1] = SPACE;
      }
   }
   if(command[0] == 'l' || command[0] == 'L') 
   {
      level = command[1] - '0';
      if(level > 0 || level < DEPTH_MAX) 
         levelColor[0] = level;
      level = command[2] - '0';
      if(level > 0 || level < DEPTH_MAX) 
         levelColor[1] = level;
      if(levelColor[1] > 0)
         return 2;
   }
   if(isdigit(command[1]) && isdigit(command[2])) 
   {
      x1 = command[1] - '0';
      y1 = command[2] - '0';
      for(x2 = -KING; x2 <= KING; ++x2) 
      {
         if(command[0] == piece_name[x2]) 
            board[y1 * 16 + x1 + 34] = (char)x2;
      }
   }
   return -1;
}


static int ChessUser(int color)
{
   char command[20];
   int count, level;
   int rc;

   for(;;) 
   {
      strcpy(command, "0000");
      count = ChessUserGraphics(command);
      if(count == 2)
      {
         level = levelColor[0];
         levelColor[0] = levelColor[1];
         levelColor[1] = level;
         return 2;
      }
      if(count)
      {
         printf("xyxy Pxy Go L## Exit %s> ", color==WHITE ? "WHITE" : "BLACK");
         rc = scanf("%15s", command);
      }
      rc = ChessUserCommand(color, command);
      if(rc >= 0)
         return rc;
      ChessShow(NULL);
      continue;
   }
}

#ifdef PNG_ONLY
void ChessDraw(int pos_selected, unsigned char *image);
int PngCreate(unsigned char *buf, unsigned char *image, int width, int height);

static void ChessShowImage(int highlight)
{
   unsigned char *buf, *image;
   int scale = 2;
   int width = 17*8*scale, height=17*8*scale;
   int bytes, x, y;
   FILE *file;
   
   buf = (unsigned char*)malloc(1024 + (width+1)*height + 16);
   if(buf == NULL)
      return;
   image = buf + 1024;
   ChessDraw(highlight, image);
   for(y = height-1; y >= 0; --y)
   {
      for(x = 0; x < width; ++x)
         image[y*(width+1) + x + 1] = image[y/2*width/2 + x/2];
   }
   bytes = PngCreate(buf, image, width, height);
#ifdef WIN32
   file = fopen("chess.png", "wb");
#else
   file = fopen("/web", "rb");
   if(file)
      fclose(file);
   else
      OS_fmkdir("/web");
   file = fopen("/web/chess.png", "wb");
#endif
   if(file)
   {
      fwrite(buf, 1, bytes, file);
      fclose(file);
   }
   else
      printf("Can't open /web/chess.png!\n");
   free(buf);
#ifndef PLASMA
   system("chess.png");
#endif   
}
#else
#define ChessShowImage(X)
#endif


static void BoardPrint(void)
{
   int x, y;
   for(y = 0; y < 8; ++y)
   {
      for(x = 0; x < 8; ++x)
         printf("%c", piece_name[board[34+y*16+x]]);
   }
}


static int ChessCommandLine(char *command)
{
   FILE *file;
   int rc;
   int color = WHITE;
   int level = 4;
   int isHtml = 0;
   char *ptr, *gui;
   int x=9, y=9;
   char string[20];

   ChessInit(board_init_pieces);
   if(strstr(command, "new") == NULL)
   {
      ptr = strstr(command, "board=");
      if(ptr)
      {
         ChessInit(ptr + 6);
      }
      else
      {
         file = fopen("chess.dat", "rb");
         if(file)
         {
            rc = fread(board, 1, sizeof(board), file);
            fclose(file);
         }
      }
   }

   gui = strstr(command, "gui");
   ptr = strstr(command, "move");
   if(ptr)
   {
      isHtml = 1;
      printf("<html><head><META HTTP-EQUIV='Pragma' CONTENT='no-cache'></head>");
      printf("<body onLoad=\"document.getElementById('firstFocus').focus()\">");
      printf("<h1>Plasma CPU Chess</h1><font size=5><pre>");
      command = ptr + 5;
      if(command[0] == '+')
         command[0] = '-';
   }
   else if(gui)
   {
      printf("<html><head><META HTTP-EQUIV='Pragma' CONTENT='no-cache'></head>\n");
      printf("<body><h1>Plasma CPU Chess</h1>\n");
      ptr = strstr(command, "gui?");
      if(ptr)
      {
         sscanf(ptr+4, "%d,%d", &x, &y);
         x /= 17*2;
         y /= 17*2;
      }
      ptr = strstr(command, "src=");
      if(ptr == NULL && x < 9)
      {
         ChessShowImage(y*16 + x + 34);
         printf("<a href=\"chess?board=");
         BoardPrint();
         printf("&src=%d%d&gui\">\n", x, y);
         printf("<img src=\"/chess.png\" alt=\"chess\" width=\"272\" height=\"272\" ismap>\n");
         printf("</a><p><a href=\"/\">Home</a></body></html>");
         return 0;
      }
      else if(x < 9)
      {
         sprintf(string, "%c%c%d%d", ptr[4], ptr[5], x, y);
         command = string;
      }
      else 
         command = "9999";
   }

   rc = ChessUserCommand(color, command);
   if(rc >= 0)
   {
      ChessMovePiece(moveBest[0], 1);
      ChessMovesInit();
      monitorSrcDst = monitor[0];
      ChessAlphaBeta(-color, 0, level * 16, -10000000, +10000000);
      ChessMovePiece(moveBest[0], 1);
   }
   if(!gui)
      ChessShow(NULL);
   if(isHtml)
   {
      printf("\n<form method='get' action='/cgi/chess'>");
      printf("Move:  <input id='firstFocus' type='text' name='move' value='' size='5'> ");
      printf("<input type='hidden' name='board' value='");
      BoardPrint();
      printf("'><input type='submit'></form>\n");
      printf("Move format is xyxy or new or piecexy.\n\n");
      printf("<a href='/'>Home</a></pre></body></html>");
   }
   else if(gui)
   {
      ChessShowImage(moveBest[depthCurrent] & 0xff);
      printf("<a href=\"chess?board=");
      BoardPrint();
      printf("&gui\">\n");
      printf("<img src=\"/chess.png\" alt=\"chess\" width=\"272\" height=\"272\" ismap>\n");
      printf("</a><p><a href=\"/\">Home</a></body></html>");
   }
   else
   {
      file = fopen("chess.dat", "wb");
      if(file)
      {
         fwrite(board, 1, sizeof(board), file);
         fclose(file);
      }
   }
   return 0;
}


int main(int argc, char *argv[])
{
   int color, value, level, i;
   int src, dst;
   unsigned int ticks;
   (void)argc; (void)argv;

   srand((unsigned int)time(NULL));
   if(argc > 1)
      return ChessCommandLine(argv[1]);
   levelColor[0] = 6;   //BLACK
   levelColor[1] = 0;   //WHITE
   printf("Chess4 %s %s level=%d\n", __DATE__, __TIME__, levelColor[0]);
   ChessInit(board_init_pieces);
   ChessDrawInit();
   ChessShow(NULL);

   color = WHITE;
   for(historyCount = 0; historyCount < TURNS_MAX && stalemate == 0; )
   {
      level = levelColor[color == WHITE];
      if(level > 0)
      {
         ChessMovesInit();
         ticks = GetTickCount();
         monitorSrcDst = monitor[0];
         value = ChessAlphaBeta(color, 0, level * 16, -10000000, +10000000);
         printf("\nvalue=%d timeMs=%d count=%d ", value, GetTickCount() - ticks, moveCount);
         if(moveBest[0] == 0)
            break;
      }
      else
      {
         value = ChessUser(color);
         if(value == 2)
            continue;
         if(value)
            break;
      }
      value = ChessMovePiece(moveBest[0], 1);
      memset(highlight, 0, sizeof(highlight));
      highlight[(moveBest[0] >> 8) & 0xff] = 1;
      highlight[moveBest[0] & 0xff] = 1;
      if(value > 0)
         kingMoved[color==WHITE] = 1;
      ChessShow(NULL);
      i = ChessUserGraphics(NULL);
      if(value < 0)
         break;
      color = -color;
      ++historyCount;
   }
   printf("Mate!\n");

   for(i = 0; i < historyCount; ++i) 
   {
      src = (history[i] >> 8) & 0xff;
      dst = history[i] & 0xff;
      printf("%d%d%d%d ", src%16-2, src/16-2, dst%16-2, dst/16-2);
   }
   printf("\ndone\n");
   getch();
   return 0;
}

