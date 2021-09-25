#include <stdio.h>
#include <cilk/cilk.h>
#include <cilk/reducer_max.h>
#include <stdlib.h>
#include <math.h>
#include <iostream>
#include <climits>
#include <cilk/cilk_api.h>
#include "timer.h"
#include <utility>
#include <vector>

using namespace std;

#define BIT 0x1

#define X_BLACK 0
#define O_WHITE 1
#define OTHERCOLOR(c) (1-(c))

#define HUMAN 'h'
#define COMPUTER 'c'
#define CHUNK_SIZE 2

#define BOARD_BIT_INDEX(row,col) ((8 - (row)) * 8 + (8 - (col)))
#define BOARD_BIT(row,col) (0x1LL << BOARD_BIT_INDEX(row,col))
#define MOVE_TO_BOARD_BIT(m) BOARD_BIT(m.row, m.col)

/* all of the bits in the row 8 */
#define ROW8 \
	(BOARD_BIT(8,1) | BOARD_BIT(8,2) | BOARD_BIT(8,3) | BOARD_BIT(8,4) |	\
	 BOARD_BIT(8,5) | BOARD_BIT(8,6) | BOARD_BIT(8,7) | BOARD_BIT(8,8))
				
/* all of the bits in column 8 */
#define COL8 \
	(BOARD_BIT(1,8) | BOARD_BIT(2,8) | BOARD_BIT(3,8) | BOARD_BIT(4,8) |	\
	 BOARD_BIT(5,8) | BOARD_BIT(6,8) | BOARD_BIT(7,8) | BOARD_BIT(8,8))

/* all of the bits in column 1 */
#define COL1 (COL8 << 7)

#define IS_MOVE_OFF_BOARD(m) (m.row < 1 || m.row > 8 || m.col < 1 || m.col > 8)
#define IS_DIAGONAL_MOVE(m) (m.row != 0 && m.col != 0)
#define MOVE_OFFSET_TO_BIT_OFFSET(m) (m.row * 8 + m.col)

typedef unsigned long long ull;

/* 
	game board represented as a pair of bit vectors: 
	- one for x_black disks on the board
	- one for o_white disks on the board
*/
typedef struct { ull disks[2]; } Board;

typedef struct { int row; int col; } Move;

typedef struct { char type; int color; bool move_possible; int depth;} Player;

class MoveComparison {
public:
  bool operator()(const pair<Move, int>& p1, const pair<Move, int>& p2) const {
	Move m1 = p1.first;
	Move m2 = p2.first;
	//cout<<m1.row<<' '<<m1.col<<' '<<p1.second<<' '<<m2.row<<' '<<m2.col<<' '<<p2.second<<"lahude"<<endl;
	if(p1.second != p2.second) return p1.second < p2.second;
	if(m1.row != m2.row) return m1.row < m2.row;
	return (m1.col < m2.col);
  }
};

Board start = { 
	BOARD_BIT(4,5) | BOARD_BIT(5,4) /* X_BLACK */, 
	BOARD_BIT(4,4) | BOARD_BIT(5,5) /* O_WHITE */
};
 
Move offsets[] = {
	{0,1}		/* right */,		{0,-1}		/* left */, 
	{-1,0}	/* up */,		{1,0}		/* down */, 
	{-1,-1}	/* up-left */,		{-1,1}		/* up-right */, 
	{1,1}		/* down-right */,	{1,-1}		/* down-left */
};

int noffsets = sizeof(offsets)/sizeof(Move);
char diskcolor[] = { '.', 'X', 'O', 'I' };


void PrintDisk(int x_black, int o_white)
{
	printf(" %c", diskcolor[x_black + (o_white << 1)]);
}

void PrintBoardRow(int x_black, int o_white, int disks)
{
	if (disks > 1) {
		PrintBoardRow(x_black >> 1, o_white >> 1, disks - 1);
	}
	PrintDisk(x_black & BIT, o_white & BIT);
}

void PrintBoardRows(ull x_black, ull o_white, int rowsleft)
{
	if (rowsleft > 1) {
		PrintBoardRows(x_black >> 8, o_white >> 8, rowsleft - 1);
	}
	printf("%d", rowsleft);
	PrintBoardRow((int)(x_black & ROW8),  (int) (o_white & ROW8), 8);
	printf("\n");
}

void PrintBoard(Board b)
{
	printf("  1 2 3 4 5 6 7 8\n");
	PrintBoardRows(b.disks[X_BLACK], b.disks[O_WHITE], 8);
}

/* 
	place a disk of color at the position specified by m.row and m,col,
	flipping the opponents disk there (if any) 
*/
void PlaceOrFlip(Move m, Board *b, int color) 
{
	ull bit = MOVE_TO_BOARD_BIT(m);
	b->disks[color] |= bit;
	b->disks[OTHERCOLOR(color)] &= ~bit;
}

/* 
	try to flip disks along a direction specified by a move offset.
	the return code is 0 if no flips were done.
	the return value is 1 + the number of flips otherwise.
*/
int TryFlips(Move m, Move offset, Board *b, int color, int verbose, int domove)
{

	Move next;
	next.row = m.row + offset.row;
	next.col = m.col + offset.col;
	
	int nflips = 0;
	Board boardAfterMove = *b;
	while(!IS_MOVE_OFF_BOARD(next)){
		ull nextbit = MOVE_TO_BOARD_BIT(next);
		if (nextbit & boardAfterMove.disks[OTHERCOLOR(color)]) {
			nflips++;
			if (verbose) printf("flipping disk at %d,%d\n", next.row, next.col);
			if (domove) PlaceOrFlip(next, &boardAfterMove, color);
		} 
		else if (nextbit & boardAfterMove.disks[color]) {
			*b = boardAfterMove;
			return nflips;
		}
		else return 0;
		next.row += offset.row;
		next.col += offset.col;
	}
	return 0;
} 

int FlipDisks(Move m, Board *b, int color, int verbose, int domove)
{
	int i;
	int nflips = 0;	

	/* try flipping disks along each of the 8 directions */
	for(i=0;i<noffsets;i++) {
		int flipresult = TryFlips(m,offsets[i], b, color, verbose, domove);
		nflips += flipresult;
	}
	return nflips;
}

void HumanTurn(Board *b, int color)
{
	
	Move m;
	ull movebit;
	for(;;) {
		printf("Enter %c's move as 'row,col': ", diskcolor[color+1]);
		scanf("%d,%d",&m.row,&m.col);
		
		/* if move is not on the board, move again */
		if (IS_MOVE_OFF_BOARD(m)) {
			printf("Illegal move: row and column must both be between 1 and 8\n");
			PrintBoard(*b);
			continue;
		}
		movebit = MOVE_TO_BOARD_BIT(m);
		
		/* if board position occupied, move again */
		if (movebit & (b->disks[X_BLACK] | b->disks[O_WHITE])) {
			printf("Illegal move: board position already occupied.\n");
			PrintBoard(*b);
			continue;
		}
		
		/* if no disks have been flipped */ 
		{
			int nflips = FlipDisks(m, b, color, 0, 1);
			if (nflips == 0) {
				printf("Illegal move: no disks flipped\n");
				PrintBoard(*b);
				continue;
			}
			FlipDisks(m, b, color, 1, 1);
			PlaceOrFlip(m, b, color);
			printf("You flipped %d disks\n", nflips);
			PrintBoard(*b);
		}
		break;
	}
}

/*
	return the set of board positions adjacent to an opponent's
	disk that are empty. these represent a candidate set of 
	positions for a move by color.
*/
Board NeighborMoves(Board b, int color)
{
	int i;
	Board neighbors = {0,0};
	for (i = 0;i < noffsets; i++) {
		ull colmask = (offsets[i].col != 0) ? 
			((offsets[i].col > 0) ? COL1 : COL8) : 0;
		int offset = MOVE_OFFSET_TO_BIT_OFFSET(offsets[i]);

		if (offset > 0) {
			neighbors.disks[color] |= 
	(b.disks[OTHERCOLOR(color)] >> offset) & ~colmask;
		} else {
			neighbors.disks[color] |= 
	(b.disks[OTHERCOLOR(color)] << -offset) & ~colmask;
		}
	}
	neighbors.disks[color] &= ~(b.disks[X_BLACK] | b.disks[O_WHITE]);
	return neighbors;
}

/*
	return the set of board positions that represent legal
	moves for color. this is the set of empty board positions  
	that are adjacent to an opponent's disk where placing a
	disk of color will cause one or more of the opponent's
	disks to be flipped.
*/
int EnumerateLegalMoves(Board b, int color, Board *legal_moves)
{
	Board neighbors = NeighborMoves(b, color);
	ull my_neighbor_moves = neighbors.disks[color];
	int row;
	int col;
	
	int num_moves = 0;
	Board zero = {0,0};
	*legal_moves = zero;	
	
	for(row=8; row >=1; row--) {
		ull thisrow = my_neighbor_moves & ROW8;
		for(col=8; thisrow && (col >= 1); col--) {
			if (thisrow & COL8) {
				Move m = { row, col };
				if (FlipDisks(m, &b, color, 0, 0) > 0) {
					legal_moves->disks[color] |= BOARD_BIT(row,col);
					num_moves++;
				}							
			}
			thisrow >>= 1;
		}
		my_neighbor_moves >>= 8;
	}
	return num_moves;
}

int CountBitsOnBoard(Board b, int color)
{
	ull bits = b.disks[color];
	int ndisks = 0;
	for (; bits ; ndisks++) {
		bits &= bits - 1; // clear the least significant bit set
	}
	return ndisks;
}

void EndGame(Board b)
{
	int o_score = CountBitsOnBoard(b,O_WHITE);
	int x_score = CountBitsOnBoard(b,X_BLACK);
	printf("Game over. \n");
	if (o_score == x_score)  {
		printf("Tie game. Each player has %d disks\n", o_score);
	} else { 
		printf("X has %d disks. O has %d disks. %c wins.\n", x_score, o_score, 
					(x_score > o_score ? 'X' : 'O'));
	}
}

int findDifference(Board b, int color){
	return CountBitsOnBoard(b, color) - CountBitsOnBoard(b, OTHERCOLOR(color));
}

bool isStartMove(Move m){
	return (m.row == -1 and m.col == -1);
}

void findBestMove(Board b, Move parent_move, int color, int rem_moves, cilk::reducer_max<pair<Move,int>, MoveComparison> &best_diff, int verbose, int mul, bool is_parent_skipped){

	
	int num_moves = 0;

	cilk::reducer_max<pair<Move, int>, MoveComparison> best_child_diff;	

	if((b.disks[color] | b.disks[OTHERCOLOR(color)]) == ULLONG_MAX  || rem_moves == 0) {
		best_diff.calc_max({parent_move, findDifference(b,color)});
		return;
	}
	
	Board neighbors = NeighborMoves(b, color);
	ull my_neighbor_moves = neighbors.disks[color];

	
	for(int row=8; row >=1; row--) {
		ull thisrow = my_neighbor_moves & ROW8;
		for(int col=8; thisrow && (col >= 1); col--) {
			if (thisrow & COL8) {
				Move m = { row, col };
				if (FlipDisks(m, &b, color, 0, 0) > 0) {
					Move legal_move = {row, col};
					Board boardAfterMove = b;

					FlipDisks(legal_move, &boardAfterMove, color, 0, 1);
					PlaceOrFlip(legal_move, &boardAfterMove, color);
				
					Move next_by_opponent;	
					if(rem_moves >= CHUNK_SIZE){
						cilk_spawn findBestMove(boardAfterMove, legal_move, OTHERCOLOR(color), rem_moves-1, best_child_diff, 0, -1, false);
					}
					else {
						findBestMove(boardAfterMove, legal_move, OTHERCOLOR(color), rem_moves-1, best_child_diff, 0, -1, false);	
					}
					num_moves++;
				}							
			}
			thisrow >>= 1;
		}
		my_neighbor_moves >>= 8;
	}
	
	cilk_sync;
	
	if(num_moves == 0LL) {	
		if(is_parent_skipped){
			best_diff.calc_max({parent_move, findDifference(b,color)});
			return;
		}

		findBestMove(b, parent_move, OTHERCOLOR(color), rem_moves, best_diff, 0, -1, true);
		return;
	}

	int diff = best_child_diff.get_value().second * mul;

	if(isStartMove(parent_move)){
		best_diff.calc_max({best_child_diff.get_value().first, diff});
	}
	else best_diff.calc_max({parent_move, diff});
	return;
		
}

void ComputerTurn(Board *b, Player *player)
{

	int color = player->color;
	cilk::reducer_max<pair<Move, int>, MoveComparison> best_diff;  
	Move best_move = {-1,-1};

	findBestMove(*b, best_move, color, player->depth, best_diff, 1, 1, true);
	best_move = best_diff.get_value().first;

	if(isStartMove(best_move)){
		printf("No legal move left for Player %d. Skipping turn\n", color+1);
		player->move_possible = false;
		return;
	}
	
	player->move_possible = true;

	int nflips = FlipDisks(best_move, b, color, 1, 1);
	PlaceOrFlip(best_move, b, color);

	printf("Move by Player %d as 'row,col': %d %d \n", color + 1, best_move.row, best_move.col);
	printf("Player %d flipped %d disks \n",  color+1, nflips);
	PrintBoard(*b);
}

bool EvaluateInputs(Player p1, Player p2){
	bool result = true;
	if(p1.type != COMPUTER and p1.type != HUMAN){
		cout<<"p1 player type is incorrect. Enter c for computer, h for human \n";
		result  = false;
	}
	if(p2.type != COMPUTER and p2.type != HUMAN){
		cout<<"p2 player type is incorrect \n";
		result = false;
	}
	if(p1.depth > 60 or p1.depth < 1){
		cout<<"search depth for computer 1 should be between 1-60 \n";
		result = false;
	}

	if(p2.depth > 60 or p2.depth < 1){
		cout<<"search depth for computer 2 should be between 1-60 \n";
		result = false;
	}

	return result;
}

void TakeTurn(Board *gameboard, Player *p){
	
	if(p->type == COMPUTER) ComputerTurn(gameboard, p);
	else HumanTurn(gameboard, p->color);
}

int main (int argc, const char * argv[]) 
{
	clock_t t;
	Board gameboard = start;

	cout<<"Enter c for computer, h for human \n";
	
	Player p1 = {'h', X_BLACK, true, 1}, p2 = {'h', O_WHITE, true, 1};
	
	cout<<"Player 1: ";
	cin>>p1.type;
	if(p1.type == COMPUTER){
		cout<<"Enter search depth for the computer 1: (At max 60): ";
		cin>>p1.depth;
	}

	cout<<"Player 2: ";	
	cin>>p2.type;	
	if(p2.type == COMPUTER){
		cout<<"Enter search depth for the computer 2: (At max 60): ";
		cin>>p2.depth;
	}
	
	if(!EvaluateInputs(p1,p2)){
		return 0;
	}

	PrintBoard(gameboard);
	t = clock();
	timer_start();
	
	do {
		TakeTurn(&gameboard, &p1);
		
		TakeTurn(&gameboard, &p2);

	} while(p1.move_possible | p2.move_possible);
	
	EndGame(gameboard);
	double seconds = timer_elapsed();
	t = clock() - t;
	double time  = t/((double)CLOCKS_PER_SEC);
	cout<<"Time taken: "<<t<<" "<<time<<" "<<seconds<<" with workers: "<<__cilkrts_get_nworkers()<<endl;
	
	return 0;
}
