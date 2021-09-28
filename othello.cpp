#include <list>
#include <stdio.h>
#include <cilk/cilk.h>
#include <cilk/reducer_max.h>
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

#define BIT 0x1

#define X_BLACK 0
#define O_WHITE 1
#define OTHERCOLOR(c) (1-(c))

#define HUMAN 'h' // to identify player as human
#define COMPUTER 'c' // to identify player as computer 
#define CHUNK_SIZE 3 // for truncating some parallel execution to increase chunk size
#define CHUNK_SIZE_1 10

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


/*
	Comparator for the reducer. The reducer used is reducer_max. 
	- The reducer consists of a pair of (Move, diff)
	- diff is the difference between the number of disks of player vs opponent
	- Move is the move which leads to that diff
	- We have to maximize the diff
	- In case difference is the same, we choose the move which has highest row and col to enforce determinism
*/

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

/*
	return the set of board positions that represent legal
	moves for color. this is the set of empty board positions  
	that are adjacent to an opponent's disk where placing a
	disk of color will cause one or more of the opponent's
	disks to be flipped.
*/
class Sum
 {
 public:
 //	Required constructor, initialize to identity (0).
 	Sum() : d_value() { }
 //   // Required reduce method
 	void reduce(Sum* other) { d_value += other->d_value; }
 //     // Two update operations
 	Sum& operator+=(const int& v) {
 		d_value |= v; 
		return *this;
 	}
 	int get_value() const { return d_value; }
          
 	private:
		int d_value;
};

void addLegalMoves(int start, int end, ull my_neighbor_moves, Board *b, int color, int verbose, int domove, cilk::reducer_opor<ull> &legal_moves){
	for(int row=end; row >= start; row--) {
                ull thisrow = my_neighbor_moves & ROW8;
                for(int col= 8 ; thisrow && (col >= 1); col--) {
                        if (thisrow & COL8) {
                                Move m = { row, col };
				int nflips = FlipDisks(m, b, color, verbose, domove);
				if(nflips > 0 ) {
					legal_moves |= BOARD_BIT(m.row, m.col);
				} 
                        }
                        thisrow >>= 1;
                }
                my_neighbor_moves >>= 8;
        }

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

/*int EnumerateLegalMoves(Board b, int color, Board *legal_moves)
{
	Board neighbors = NeighborMoves(b, color);
	ull my_neighbor_moves = neighbors.disks[color];
	
	int num_moves = 0;
	int possible_moves = CountBitsOnBoard(neighbors, color);
	//cilk::reducer_opor<ull> possible_legal_moves(0);	

	//cout<<"lahude "<<possible_moves<<endl;
	if(possible_moves >= CHUNK_SIZE_1){

		cilk_spawn addLegalMoves(5, 8 , (my_neighbor_moves), &b, color, 0, 0, possible_legal_moves);
		//cilk_spawn addLegalMoves(5, 6 , (my_neighbor_moves >> 16), &b, color, 0, 0, possible_legal_moves);
		cilk_spawn addLegalMoves(1, 4 , (my_neighbor_moves >> 32), &b, color, 0, 0, possible_legal_moves);
		//cilk_spawn addLegalMoves(1, 2 , (my_neighbor_moves >> 48), &b, color, 0, 0, possible_legal_moves);
	}
	else {
		
		addLegalMoves(4, 8 , my_neighbor_moves, &b, color, 0, 0, possible_legal_moves);

		addLegalMoves(1, 4 , (my_neighbor_moves >> 32), &b, color, 0, 0, possible_legal_moves);
	}	
	//cilk_sync;	

	legal_moves->disks[color] = possible_legal_moves.get_value();

	//cout<<"lahude 2 "<<possible_legal_moves.get_value(); 
	return CountBitsOnBoard(*legal_moves, color);
}*/

void EnumerateLegalMoves(Board b, int color, Board &legal_moves){
	cilk::reducer_opor<ull> reducer_legal_moves;
	legal_moves.disks[color] = 0;
	ull occupied_disks = b.disks[color] | b.disks[OTHERCOLOR(color)];
	for(int row = 8; row >= 1; row--){
		Board bb = b;
		for(int col = 8; col >=1; col--){
			Move m = {row, col};
			if(((occupied_disks & MOVE_TO_BOARD_BIT(m)) == 0) && FlipDisks(m, &bb, color, 0, 0) > 0 ){
				legal_moves.disks[color] |= MOVE_TO_BOARD_BIT(m);	
			}
		}
	}
	//legal_moves.disks[color] = reducer_legal_moves.get_value();
	return;
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

/* check if this is the first iteration in the search tree
	Usage - helps us to find if the move has to be skipped by the player when there is no legal move left
*/
bool isStartMove(Move m){
	return (m.row == -1 and m.col == -1);
}

/* finds best move by the computer.
	b - current board config
	parent_move - what was the move earlier to this iteration.
	color - color of the player
	rem_moves - remaining iterations to search for 
	best_diff - cilk reducer (passed by the parent call) which stores the best difference found by this iteration.
		The reducer stores the parent_move and the difference. 
	mul - factor by which the best diff has to be multiplied to return the diff to the parent. This is in 
		context with negamax algorithm. The max for player is -1*(max of opponent) assuming both play optimally. 
*/
int findBestMove(Board b, int color, int depth, int rem_moves, int verbose, int mul, bool is_parent_skipped, Move &best_move){

	Move no_move = {-1, -1};
	Board legal_moves_vector;
	EnumerateLegalMoves(b, color, legal_moves_vector);	
	//vector<Move> legal_moves_vector(legal_moves.begin(), legal_moves.end());	
	//cout<<rem_moves<<"bhusdix "<<color<<" "<<legal_moves.get_value().size()<<endl;
	int num_moves = CountBitsOnBoard(legal_moves_vector, color);
	cilk::reducer_max<int> best_child_diff;	
	int max=-65;
	//for(list<Move>::const_iterator legal_move = legal_moves.begin(); legal_move != legal_moves.end(); legal_move++){
	if(depth != 1){ 
		cilk_for(int i=0; i<num_moves; i++){
			Board boardAfterMove = b;
			ull legal_moves = legal_moves_vector.disks[color];
			ull a = 1;
			for(int j=0; j < i; j++){
				
				legal_moves ^= (a << __builtin_ctzll(legal_moves));
			}
			int lowestSetBit =  __builtin_ctzll(legal_moves);
			Move legal_move = {8-lowestSetBit/8, 8-lowestSetBit%8};
		//cout<<";laud "<< legal_move->row<<" "<<legal_move->col<<" "<<rem_moves<<endl;
		//FlipDisks(legal_move, &boardAfterMove, color, 0, 1);
			FlipDisks(legal_move, &boardAfterMove, color, 0, 1);
			  	  //legal_moves.push_back(legal_move);
			PlaceOrFlip(legal_move, &boardAfterMove, color);                      
			//if(rem_moves >= CHUNK_SIZE)
			if(rem_moves==depth) best_child_diff.calc_max(findDifference(boardAfterMove, color));
			else {
				int best_child_move = findBestMove(boardAfterMove, OTHERCOLOR(color), depth+1, rem_moves, 0, -1, false, best_move);	  	  	  
			//else findBestMove(boardAfterMove, *legal_move, OTHERCOLOR(color), rem_moves-1, best_child_diff, 0, -1, false);
				best_child_diff.calc_max(best_child_move);
			}
		}			
	}
	else{
		for(int i=0; i<num_moves; i++){
			Board boardAfterMove = b;
			ull legal_moves = legal_moves_vector.disks[color];
			ull a = 1;
			for(int j=0; j < i; j++){
				
				legal_moves ^= (a << __builtin_ctzll(legal_moves));
			}
			int lowestSetBit =  __builtin_ctzll(legal_moves);
			Move legal_move = {8-lowestSetBit/8, 8-lowestSetBit%8};
		//cout<<";laud "<< legal_move->row<<" "<<legal_move->col<<" "<<rem_moves<<endl;
		//FlipDisks(legal_move, &boardAfterMove, color, 0, 1);
			FlipDisks(legal_move, &boardAfterMove, color, 0, 1);
			  	  //legal_moves.push_back(legal_move);
			PlaceOrFlip(legal_move, &boardAfterMove, color);                      
			//if(rem_moves >= CHUNK_SIZE)
			int diff; 
			if(depth == rem_moves) diff = findDifference(boardAfterMove, color);
			else diff = findBestMove(boardAfterMove, OTHERCOLOR(color), depth+1, rem_moves, 0, -1, false, best_move);	
			if(diff > max){
  	  	  		max=diff;
				best_move = legal_move;
			}
			//else findBestMove(boardAfterMove, *legal_move, OTHERCOLOR(color), rem_moves-1, best_child_diff, 0, -1, false);
				//best_child_diff.calc_max({legal_move, best_child_move.second});
		}			
	}
	//cilk_sync;
	/* if there are no legal moves left then there can be 3 cases:
		1. Even the first turn cannot be played by the player. In this we just skip.
		2. Keep on searching the next best move by the opponent and try to minimize this. 
		3. If there are no moves left even for opponent then the game has to end.
	*/
	if(num_moves == 0LL) {
	
		//Board opponent_moves;
		//EnumerateLegalMoves(b,1-color, opponent_moves);
		if(is_parent_skipped){
			return findDifference(b,color)*mul;
		}

		else return findBestMove(b, OTHERCOLOR(color), depth, rem_moves, 0, -1, true, best_move);
	}
	
	/* store the corresponding moves and difference*/
	int diff = best_child_diff.get_value() * mul;
	
//	cout<<"koi pahycnha ki nahi "<<num_moves<<' '<<diff<<endl;
	return diff;
}

void ComputerTurn(Board *b, Player *player)
{

	int color = player->color;

//	cilk::reducer_max<pair<Move, int>, MoveComparison> best_diff;  
	
	/* start case when best move is unknown or not possible */
	Move best_move = {-1,-1};

	int best_diff= findBestMove(*b, color, 1, player->depth, 0, 1, true, best_move);
	//best_move = best_diff.first;
	/* if the best move is not possible then skip turn else print it*/
	
	//EnumerateLegalMoves(*b, color, legal_moves);
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


/*	1. Ask for inputs
		2. Evaluate them
		3. Make the players play their move else skip
		4. End the game if no legal move exists for both players and print final result
		5. print the time for the game execution along with number of workers. 
*/
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
