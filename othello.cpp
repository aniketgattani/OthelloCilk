#include <stdlib.h>
#include <math.h>
#include <iostream>
#include <climits>

using namespace std;

#define BIT 0x1

#define X_BLACK 0
#define O_WHITE 1
#define OTHERCOLOR(c) (1-(c))

#define HUMAN 'h'
#define COMPUTER 'c'


/* 
	represent game board squares as a 64-bit unsigned integer.
	these macros index from a row,column position on the board
	to a position and bit in a game board bitvector
*/
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

typedef struct { char type; int number; bool move_possible;} Player;


Board start = { 
	BOARD_BIT(4,4) | BOARD_BIT(5,5) /* X_BLACK */, 
	BOARD_BIT(4,5) | BOARD_BIT(5,4) /* O_WHITE */
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

	if (!IS_MOVE_OFF_BOARD(next)) {
		ull nextbit = MOVE_TO_BOARD_BIT(next);
		if (nextbit & b->disks[OTHERCOLOR(color)]) {
			int nflips = TryFlips(next, offset, b, color, verbose, domove);
			if (nflips) {
	if (verbose) printf("flipping disk at %d,%d\n", next.row, next.col);
	if (domove) PlaceOrFlip(next,b,color);
	return nflips + 1;
			}
		} else if (nextbit & b->disks[color]) return 1;
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
		nflips += (flipresult > 0) ? flipresult - 1 : 0;
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
			int nflips = FlipDisks(m, b, color, 1, 1);
			if (nflips == 0) {
				printf("Illegal move: no disks flipped\n");
				PrintBoard(*b);
				continue;
			}
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
	*legal_moves = {0,0};
	
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

bool CheckIfMoveIsPossible(Board b, int color)
{
	Board legal_moves;
	int num_moves = EnumerateLegalMoves(b, color, &legal_moves);
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

int findBestMove(Board b, int color, int rem_moves, Move *m, int verbose){

	Board legal_moves;

	int num_moves = EnumerateLegalMoves(b, color, &legal_moves);

	int max_diff = -64;

	if((b.disks[color] | b.disks[OTHERCOLOR(color)]) == ULLONG_MAX) return max_diff;

	
	if(legal_moves.disks[color] == 0) {
		return (-1*findBestMove(b, OTHERCOLOR(color), rem_moves, m, 0));
	}

	Move best_move = {-1,-1};

	for(int row = 8; row >=1; row--) {
		for(int col = 8; col >=1; col--) {
			// legal move exists
			if(legal_moves.disks[color] & BOARD_BIT(row, col)){
				Move legal_move = {row, col};
				Board boardAfterMove = b;

				// if(verbose)
				cout<<row<<" ajncsd "<<col<<" "<<rem_moves<<endl;

				int nflips = FlipDisks(legal_move, &boardAfterMove, color, 0, 1);
				PlaceOrFlip(legal_move, &boardAfterMove, color);

				int diff = findDifference(boardAfterMove, color);
				
				Move next_by_opponent;
				
				if(rem_moves > 1) {
					diff = -1*findBestMove(boardAfterMove, OTHERCOLOR(color), rem_moves-1, &next_by_opponent, 0);		
				}

				if(max_diff < diff){
					best_move = legal_move;
					max_diff = diff;
				}
			}
		}
	}

	*m = best_move;
	return max_diff;
}

void ComputerTurn(Board *b, int color, int player, int search_depth)
{
	Move best_move;
	findBestMove(*b, color, search_depth, &best_move, 1);

	int nflips = FlipDisks(best_move, b, color, 1, 1);
	PlaceOrFlip(best_move, b, color);
	
	printf("Move by Player %d as 'row,col': %d %d \n", player, best_move.row, best_move.col);
	printf("Player %d flipped %d disks\n", player, nflips);
		
	PrintBoard(*b);
}

bool EvaluateInputs(Player p1, Player p2, int search_depth){
	bool result = true;
	if(p1.type != COMPUTER and p1.type != HUMAN){
		cout<<"p1 player type is incorrect. Enter c for computer, h for human \n";
		result  = false;
	}
	if(p2.type != COMPUTER and p2.type != HUMAN){
		cout<<"p2 player type is incorrect \n";
		result = false;
	}
	if(search_depth > 7 or search_depth < 1){
		cout<<"search depth should be between 1-7 \n";
		result = false;
	}
	return result;
}


int main (int argc, const char * argv[]) 
{
	Board gameboard = start;
	int search_depth = 1;
	cout<<"Enter c for computer, h for human \n";
	Player p1 = {'h', 1, true}, p2 = {'h', 2, true};
	cout<<"Player 1: ";
	cin>>p1.type;
	cout<<"Player 2: ";
	cin>>p2.type;
	if(p1.type == COMPUTER || p2.type == COMPUTER){
		cout<<"Enter search depth for the computer: (At max 7): ";
		cin>>search_depth;
	}
	
	if(!EvaluateInputs(p1,p2,search_depth)){
		return 0;
	}

	PrintBoard(gameboard);
	do {
		if(p1.move_possible){
			if(p1.type == COMPUTER) ComputerTurn(&gameboard, X_BLACK, p1.number, search_depth);
			else HumanTurn(&gameboard, X_BLACK);
		}
		
		p2.move_possible = 
			CheckIfMoveIsPossible(gameboard, O_WHITE);
		
		if(p2.move_possible){
			if(p2.type == COMPUTER) ComputerTurn(&gameboard, O_WHITE, p2.number, search_depth);
			else HumanTurn(&gameboard, O_WHITE);
		}

		p1.move_possible = 
			CheckIfMoveIsPossible(gameboard, X_BLACK);
		
	} while(p1.move_possible | p2.move_possible);
	
	EndGame(gameboard);
	
	int x=0;
	scanf("%d",&x);
	return 0;
}
