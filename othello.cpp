#include <list>
#include <stdio.h>
#include <cilk/cilk.h>
#include <cilk/reducer_max.h>
#include <cilk/reducer_opadd.h>
#include <cilk/reducer_opor.h>
#include <stdlib.h>
#include <math.h>
#include <iostream>
#include <climits> // for UULONG_MAX
#include <cilk/cilk_api.h>
#include "timer.h" // to calculate time taken by program in UNIX
#include <cilk/reducer_list.h>
#include <vector>
#include <utility> // for using std::pair
using namespace std;

double seconds = 0;
int VERBOSE = 1;
double iterations = 0;
double tot_num_moves = 0;
#define BIT 0x1

#define X_BLACK 0
#define O_WHITE 1
#define OTHERCOLOR(c) (1-(c))

#define HUMAN 'h' // to identify player as human
#define COMPUTER 'c' // to identify player as computer 
#define CHUNK_SIZE 3 // for truncating some parallel execution to increase chunk size

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


/*
	Player class that recognizes each player in the Reversi game
	- type - to identify if the player is computer or human
	- color - to identify the color chosen by the player X or O
	- move_possible - to identify if there is a further move is possible by the player
	- depth - (only for computer player) The search depth to evaluate which maximizes its winning probability 
*/
typedef struct { char type; int color; bool move_possible; int depth;} Player;


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

/* helper functions to print board configuration */
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
	
	Examples of a valid flip:
		Let there be the following order of disks in the given offset direction (color = X):
		. O O X
		if we place X at the leftmost position:
		X X X X
	
	Examples of invalid flip (color = X):
		1. . O O O
		2. . O . X
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
		/* you should flip a disk only if it already has a disk of the opponent */
		if (nextbit & boardAfterMove.disks[OTHERCOLOR(color)]) {
			nflips++;
			if (verbose) printf("flipping disk at %d,%d\n", next.row, next.col);
			if (domove) PlaceOrFlip(next, &boardAfterMove, color);
		} 
		/* if you have finally encountered a disk of the same color then we should stop*/
		else if (nextbit & boardAfterMove.disks[color]) {
			*b = boardAfterMove;
			return nflips;
		}
		/* all disks between a flip should be already occupied. Else there can't be any flip */
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

class MoveComparison {
public:
  bool operator()(const pair<Move, int>& p1, const pair<Move, int>& p2) const {
	Move m1 = p1.first;
	Move m2 = p2.first;
	if(p1.second != p2.second) return p1.second < p2.second;
	if(m1.row != m2.row) return m1.row > m2.row;
	return (m1.col > m2.col);
  }
};

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
		
		{
			/* First check if disks have been flipped 
				If they have, then perform actual move printing all flipped disks
				else just ask the human to enter move again
			*/ 
			int nflips = FlipDisks(m, b, color, 0, 0);
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
bool isOccupied(Board *b, Move m){
	
	ull occupied_disks = (b->disks[O_WHITE] | b->disks[X_BLACK]);
	return (occupied_disks & MOVE_TO_BOARD_BIT(m)) > 0LL;	
}
void addLegalMoves(Board *b, int color, int verbose, int domove, Board *legal_moves){
	cilk::reducer_opor<ull> legal_moves_reducer;
	for(int row= 8; row >= 1; row--) {
		//ull neighbor_moves = my_neighbor_moves;
		//neighbor_moves >>= (8-row)*8;
        	//ull thisrow = neighbor_moves & ROW8;
        	for(int col= 8 ; (col >= 1); col--) {
            		Move m = {row, col};
			if (!isOccupied(b, m)) {
				Board boardBeforeMove = *b;
				int nflips = FlipDisks(m, &boardBeforeMove, color, verbose, domove);
				if(nflips > 0 ) {
					legal_moves_reducer|= BOARD_BIT(m.row, m.col);
				} 
            		}
            		//thisrow >>= 1;
        	}
    	}
   	legal_moves->disks[color] = legal_moves_reducer.get_value();
	return;
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

int EnumerateLegalMoves(Board b, int color, Board *legal_moves)
{
	Board neighbors;
	//NeighborMoves(b, color);
	//ull my_neighbor_moves = neighbors.disks[color];
	
	int num_moves = 0;
	int num_neighbors = CountBitsOnBoard(neighbors, color);

	addLegalMoves(&b, color, 0, 0, legal_moves);

	return CountBitsOnBoard(*legal_moves, color);
}


int findDifference(Board b, int color){
	return CountBitsOnBoard(b, color) - CountBitsOnBoard(b, OTHERCOLOR(color));
}

/* check if this is the first iteration in the search tree
	Usage - helps us to find if the move has to be skipped by the player when there is no legal move left
*/
bool isStartMove(Move m){
	return (m.row == -1 and m.col == -1);
}

/* finds best move by the computer.
	b - current board config
	color - color of the player
	depth - current depth of the iteration in the search tree 
	search_depth - max search depth provided by the user 
	best_diff - cilk reducer (passed by the parent call) which stores the best difference found by this iteration.
		The reducer stores the parent_move and the difference. 
	mul - factor by which the best diff has to be multiplied to return the diff to the parent. This is in 
		context with negamax algorithm. The max for player is -1*(max of opponent) assuming both play optimally. 
*/
int findBestMove(Board b, int color, int depth, int search_depth, int mul, bool is_parent_skipped, Move &best_move){

	Move no_move = {-1, -1};
	Board legal_moves = {0,0};
	int num_moves = EnumerateLegalMoves(b, color, &legal_moves);	
	
	cilk::reducer_max<int> best_diff;
	cilk::reducer_max<pair<Move, int>, MoveComparison> best_move_reducer;
	int max_diff = -65;
	best_move_reducer.calc_max({no_move, max_diff});	
	cilk_for(int i = 0; i < num_moves; i++) {
		ull legal_moves_ull = legal_moves.disks[color];
		for(int j=0; j < i; j++){		
			legal_moves_ull ^= (1LL << __builtin_ctzll(legal_moves_ull));
		}
		int lowestSetBit =  __builtin_ctzll(legal_moves_ull);
		Move legal_move = {8-lowestSetBit/8, 8-lowestSetBit%8};
		
		if (!isOccupied(&b, legal_move)) {
			Board boardAfterMove = b;
			int nflips = FlipDisks(legal_move, &boardAfterMove, color, 0, 1);
			if(nflips == 0) continue;
			PlaceOrFlip(legal_move, &boardAfterMove, color);
			int diff;                      
			if(search_depth==depth) diff = findDifference(boardAfterMove, color);
			else {
				diff = findBestMove(boardAfterMove, OTHERCOLOR(color), depth+1, search_depth, -1, false, best_move);	  	  	  
			}
			best_diff.calc_max(diff);				 
            		if(depth==1) best_move_reducer.calc_max({legal_move, diff});
        	}
    	}
	
	//tot_num_moves += num_moves;
	//iterations++;
		
	/* if there are no legal moves left then there can be 3 cases:
		1. Keep on searching the next best move by the opponent and try to minimize this. 
		2. If there were no moves left even for opponent in previous iteration 
		(is_parent_skipped == true) then the search tree has to end.
	*/
	if(num_moves == 0) {	
		if(is_parent_skipped){
			best_diff.calc_max(findDifference(b,color));
		}

		else best_diff.calc_max(findBestMove(b, OTHERCOLOR(color), depth, search_depth, -1, true, best_move));
	}
	
	/* store the corresponding moves and difference*/
	if(depth==1) best_move = best_move_reducer.get_value().first;
	return best_diff.get_value() * mul;	
}

void ComputerTurn(Board *b, Player *player)
{

	int color = player->color;
	
	/* start case when best move is unknown or not possible */
	Move best_move = {-1,-1};

	int best_diff= findBestMove(*b, color, 1, player->depth, 1, true, best_move);

	/* if the best move is not possible then skip turn else print it*/
	if(isStartMove(best_move)){
		if(VERBOSE) printf("No legal move left for Player %d. Skipping turn\n", color+1);
		player->move_possible = false;
		return;
	}

	player->move_possible = true;

	int nflips = FlipDisks(best_move, b, color, 1, 1);
	PlaceOrFlip(best_move, b, color);
	if(VERBOSE){
		printf("Move by Player %d as 'row,col': %d %d \n", color + 1, best_move.row, best_move.col);
		printf("Player %d flipped %d disks \n",  color+1, nflips);
		PrintBoard(*b);
	}
}


/* evaluate the inputs given to the program
		- The search depth should be within range (1-60)
		- The player types should be either h or c
*/
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


/*	1. Ask for inputs
		2. Evaluate them
		3. Make the players play their move else skip
		4. End the game if no legal move exists for both players and print final result
		5. print the time for the game execution along with number of workers. 
*/
int main (int argc, const char * argv[]) 
{

	if ((argc >=2)) VERBOSE = atoi(argv[1]);

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
	
	timer_start();
	
	do {
		TakeTurn(&gameboard, &p1);
		
		TakeTurn(&gameboard, &p2);

	} while(p1.move_possible | p2.move_possible);
	
	seconds += timer_elapsed();
	EndGame(gameboard);
	
	cout<<"Time taken: "<<seconds<<"avg "<<(tot_num_moves/iterations)<<" with workers: "<<__cilkrts_get_nworkers()<<endl;
	
	return 0;
}
