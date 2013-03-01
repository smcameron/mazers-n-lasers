/*
    (C) Copyright 2013, Stephen M. Cameron.

    This file is part of mazers-n-lasers.

    mazers-n-lasers is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    mazers-n-lasers is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with mazers-n-lasers; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <math.h>

#include "libol.h"
#include "joystick.h"
#include "my_point.h"
#include "snis_alloc.h"

#define SCREEN_WIDTH (1000.0)
#define SCREEN_HEIGHT (1000.0)
#define XSCALE (1.0 / (SCREEN_WIDTH / 2.0))
#define YSCALE (-1.0 / (SCREEN_HEIGHT / 2.0))

#define RED 0xFF0000
#define GREEN 0x00FF00
#define BLUE 0x0000FF
#define CYAN (BLUE | GREEN) 
#define MAGENTA (RED | BLUE)
#define YELLOW (RED | GREEN)

static int levelcolor[] = {
	RED,
	BLUE,
	CYAN,
	YELLOW,
	MAGENTA,
	GREEN,
};

/* Maze dimensions, in units of chars */
#define XDIM 70
#define YDIM 20

static int xo[] = { 0, 1, 0, -1 };
static int yo[] = { -1, 0, 1, 0 };

static int playerx, playery, playerdir, playerlevel;

static int requested_forward = 0;
static int requested_backward = 0;
static int requested_left = 0;
static int requested_right = 0;
static int requested_button_zero = 0;
static int attract_mode_active = 1;
#define JOYSTICK_DEVICE "/dev/input/js0"
static int joystick_fd = -1;

struct object;

typedef void (*move_function)(struct object *o, char *maze, float time);
typedef void (*draw_function)(struct object *o, int sx, int sy, float scale);

#define LADDERS_BETWEEN_LEVELS 5 
#define MAXLEVELS 5
#define MAXOBJS 1000
static int nrobots = 20;
static struct object {
	int x, y, level, alive, n;
	struct my_vect_obj *v;
        move_function move;
        draw_function draw;
	float time_since_last_move;
	int direction;
} o[MAXOBJS];
static int nobjs = 0;
struct snis_object_pool *obj_pool;
int openlase_color = GREEN;
int wallcolor = GREEN;

#define SHRINKFACTOR (0.8)
#define BASICX 100
#define BASICY 100
#define NSTEPS 8
static float shrinkfactor[NSTEPS] = { 0 };

struct my_point_t robot_points[] =
#include "robot-vertices.h"
struct my_vect_obj robot_vect;

struct my_point_t up_ladder_points[] =
#include "up-ladder-vertices.h"
struct my_vect_obj up_ladder_vect;

struct my_point_t down_ladder_points[] =
#include "down-ladder-vertices.h"
struct my_vect_obj down_ladder_vect;

/* get a random number between 0 and n-1... fast and loose algorithm.  */
static inline int randomn(int n)
{
	return random() % n;
}

static float maze_density(char *maze, int xdim, int ydim)
{
	int i, j;
	float total = 0.0;

	for (i = 0; i < ydim; i++)
		for (j = 0; j < xdim; j++)
			if (maze[i * xdim + j] == '#')
				total = total + 1.0;
	return total / (float) (xdim * ydim);
}

static void print_maze(char *maze, int xdim, int ydim)
{
	int i, j;

	for (i = 0; i < ydim; i++) {
		for (j = 0; j < xdim; j++) {
			if (i == playery && j == playerx) {
				switch (playerdir) {
				case 0: printf("^");
					break;
				case 1: printf(">");
					break;
				case 2: printf("v");
					break;
				case 3: printf("<");
					break;
				default:
					printf("?");
					break;
				}
			} else {
				printf("%c", maze[i * xdim + j]);
			}
		}
		printf("\n");
	}
}

#define mazesize(xdim, ydim) \
	(sizeof(char) * xdim * ydim)

static int inbounds_for_digging(int x, int y, int xdim, int ydim)
{
	if (x < 1 || x >= xdim -1 || y < 1 || y >= ydim -1)
		return 0;
	return 1;
}

static int inbounds(int x, int y, int xdim, int ydim)
{
	if (x < 0 || x >= xdim || y < 0 || y >= ydim)
		return 0;
	return 1;
}

static int ok_to_dig(char *maze, int x, int y, int direction,
			int xdim, int ydim)
{
	int left, right;

	x += xo[direction];
	y += yo[direction];

	if (!inbounds_for_digging(x, y, xdim, ydim))
		return 0;
	if (maze[y * xdim + x] != '.')
		return 0;
	left = direction - 1;
	if (left < 0)
		left = 3;
	if (maze[(y + yo[left]) * xdim + x + xo[left]] != '.')
		return 0;
	right = direction + 1;
	if (right > 3)
		right = 0;
	if (maze[(y + yo[right]) * xdim + x + xo[right]] != '.')
		return 0;
	return 1;
}

static void dig(char *maze, int x, int y, int direction, int xdim, int ydim)
{
	int left, right;

	maze[y * xdim + x] = '#';

	if (randomn(100) < 7)
		return;

	if (ok_to_dig(maze, x, y, direction, xdim, ydim))
		dig(maze, x + xo[direction], y + yo[direction],
			direction, xdim, ydim);
	if (randomn(100) < 20) {
		left = direction - 1;
		if (left < 0)
			left = 3;
		if (ok_to_dig(maze, x, y, left, xdim, ydim))
			dig(maze, x + xo[left], y + yo[left], left, xdim, ydim);
	}
	if (randomn(100) < 20) {
		right = direction + 1;
		if (right > 3)
			right = 0;
		if (ok_to_dig(maze, x, y, right, xdim, ydim))
			dig(maze, x + xo[right], y + yo[right], right, xdim, ydim);
	}
}

static char *make_maze(int xdim, int ydim, int startx, int starty, int startdir)
{
	char *maze;
	float density;

	for (;;) {
		maze = malloc(mazesize(xdim, ydim));
		memset(maze, '.', mazesize(xdim, ydim)); 
		dig(maze, startx, starty, startdir, xdim, ydim);
		density = maze_density(maze, xdim, ydim);
		if (density > 0.30)
			break;
		free(maze);
	}
	return maze;
}

static int setup_openlase(void)
{
	OLRenderParams params;

	memset(&params, 0, sizeof params);
	params.rate = 48000;
	params.on_speed = 2.0/100.0;
	params.off_speed = 2.0/20.0;
	params.start_wait = 12;
	params.start_dwell = 3;
	params.curve_dwell = 0;
	params.corner_dwell = 12;
	params.curve_angle = cosf(30.0*(M_PI/180.0)); // 30 deg
	params.end_dwell = 3;
	params.end_wait = 10;
	params.snap = 1/100000.0;
	params.render_flags = RENDER_GRAYSCALE;

	if (olInit(3, 60000) < 0) {
		fprintf(stderr, "Failed to initialized openlase\n");
		return -1;
	}
	olSetRenderParams(&params);

	olLoadIdentity();
	olTranslate(-1,1);
	olScale(XSCALE, YSCALE);

	return 0;
}

void draw_generic(struct object *o, int sx, int sy, float scale)
{
	int j;
	int x1, y1, x2, y2;

	if (o->v->p == NULL)
		return;

	x1 = sx + o->v->p[0].x * scale;
	y1 = sy + o->v->p[0].y * scale;  

	olBegin(OL_LINESTRIP);
	olVertex(x1, y1, openlase_color);

	for (j = 0; j < o->v->npoints - 1; j++) {
		if (o->v->p[j+1].x == LINE_BREAK) { /* Break in the line segments. */
			j += 2;
			x1 = sx + o->v->p[j].x * scale;
			y1 = sy + o->v->p[j].y * scale;  
			olVertex(x1, y1, C_BLACK);
		}
		if (o->v->p[j].x == COLOR_CHANGE) {
			/* do something here to change colors */
			j += 1;
			x1 = sx + o->v->p[j].x * scale;
			y1 = sy + o->v->p[j].y * scale;  
		}
		x2 = sx + o->v->p[j + 1].x * scale; 
		y2 = sy + o->v->p[j + 1].y * scale;
		if (x1 > 0 && y2 > 0)
			olVertex(x2, y2, openlase_color);
		x1 = x2;
		y1 = y2;
	}
	olEnd();
}

static void draw_objects(char *maze, int xdim, int ydim)
{
	int i, j, x, y;

	x = playerx;
	y = playery;

	for (i = 0; i < NSTEPS; i++) {
		for (j = 0; j < nobjs; j++) {
			if (o[j].level == playerlevel && 
				x == o[j].x && y == o[j].y) {
				o->draw(&o[j], 500 - (500 * shrinkfactor[i]),
						500 + 500 * shrinkfactor[i],
						 2.0 * shrinkfactor[i]);
			}
		}
		x += xo[playerdir];
		y += yo[playerdir];
		if (!inbounds(x, y, xdim, ydim))
			break;
		if (maze[y * xdim + x] == '.') /* wall */
			break;
	}
}

static void climb_ladder(char *maze)
{
	int i;

	requested_button_zero = 0;
	for (i = 0; i < nobjs; i++) {
		if (o[i].level != playerlevel)
			continue;
		if (o[i].x != playerx)
			continue;
		if (o[i].y != playery)
			continue;
		if (o[i].v == &up_ladder_vect) {
			if (playerlevel > 0) {
				playerlevel--;
				return;
			}
		} else {
			if (o[i].v == &down_ladder_vect) {
				if (playerlevel < MAXLEVELS - 1) {
					playerlevel++;
					return;
				}
			}
		}
	}
}

static void move_player(char *maze, int xdim, int ydim)
{
	static unsigned long last_move_usec = 0;
	static unsigned long last_move_sec = 0;
	struct timeval tv;
	int nx, ny, nd, tx, ty, i;
	int dir;

	nx = playerx;
	ny = playery;
	nd = playerdir;

	gettimeofday(&tv, NULL);
	if (tv.tv_usec - last_move_usec < 250000 && tv.tv_sec == last_move_sec)
		return;

	if (requested_forward) {
		tx = playerx + xo[playerdir];
		ty = playery + yo[playerdir];
		if (!inbounds_for_digging(tx, ty, xdim, ydim))
			return;
		if (maze[ty * xdim + tx] == '#') {
			nx = tx;
			ny = ty;
		}
	}

	if (requested_backward) {
		dir = playerdir + 2;
		if (dir > 3)
			dir -= 4;	
		tx = playerx + xo[dir];
		ty = playery + yo[dir];
		if (!inbounds_for_digging(tx, ty, xdim, ydim))
			return;
		if (maze[ty * xdim + tx] == '#') {
			nx = tx;
			ny = ty;
		}
	}

	if (requested_left) {
		nd = playerdir - 1;
		if (nd < 0)
			nd = 3;
	}

	if (requested_right) {
		nd = playerdir + 1;
		if (nd > 3)
			nd = 0;
	}

	if (requested_button_zero) /* climb ladder */
		climb_ladder(maze);
	
	if (nx != playerx || ny != playery || nd != playerdir) {
		playerx = nx;
		playery = ny;
		playerdir = nd;
		last_move_usec = tv.tv_usec;
		last_move_sec = tv.tv_sec;
#if 0
		/* activate this to debug player movement */
		print_maze(maze, xdim, ydim);
#endif
	}
}

static void move_objects(char *maze, int xdim, int ydim, float elapsed_time)
{
	int i;

	move_player(maze, xdim, ydim);
	
	for (i = 0; i < nobjs; i++) {
		o[i].move(&o[i], maze, elapsed_time);
	}
}

static void attract_mode(void)
{
}

static float init_shrinkfactor(int n)
{
	int i;
	float f = 1.0;

	for (i = 0; i < n; i++) {
		shrinkfactor[i] = f;
		f = f * SHRINKFACTOR;
	}
	return f;
}
/* This is the cheeziest 3d dungeon renderer ever, a re-implementation of what
 * old DOS games like Wizardry and early Ultima games did, 'cept nowadays we
 * can use floats with impunity
 */
static void draw_maze(char *maze, int xdim, int ydim,
			int playerx, int playery, int playerdir)
{
	int steps = NSTEPS;
	int i, x, y, left, right;
	int x1, y1, x2, y2;
	int sf;

	wallcolor = levelcolor[playerlevel];

	/* draw top of left wall */
	x = playerx;
	y = playery;
	x1 = 0;
	y1 = 0;
	sf = 0;
	x2 = x1 + BASICX * shrinkfactor[sf];
	y2 = y1 + BASICY * shrinkfactor[sf];
	left = playerdir - 1;
	if (left < 0)
		left = 3;	
	for (i = 0; i < steps; i++) {
		if (!inbounds(x + xo[left], y + yo[left], xdim, ydim))
			continue;
		if (maze[(y + yo[left]) * xdim + x + xo[left]] == '.') {
			olLine(x1, y1, x2, y2, wallcolor);
		} else {
			olLine(x1, y2, x2, y2, wallcolor);
			olLine(x1, SCREEN_HEIGHT - y2, x2,
				SCREEN_HEIGHT - y2, wallcolor);
			olLine(x2, y2, x2, SCREEN_HEIGHT - y2, wallcolor);
			olLine(x1, y1, x1, SCREEN_HEIGHT - y1, wallcolor);
		}
		x += xo[playerdir];
		y += yo[playerdir];
		if (maze[y * xdim + x] == '.') { /* back wall */
			olLine(x2, y2, SCREEN_WIDTH - x2, y2, wallcolor);
			olLine(x2, SCREEN_HEIGHT - y2,
				SCREEN_WIDTH - x2, SCREEN_HEIGHT - y2, wallcolor);

			/* FIXME: these next 2 lines sometimes get drawn 2x */
			olLine(x2, y2, x2, SCREEN_HEIGHT - y2, wallcolor);
			olLine(SCREEN_WIDTH - x2, y2,
				SCREEN_WIDTH - x2, SCREEN_HEIGHT - y2, wallcolor);
			break;
		}
		x1 = x2;
		y1 = y2;
		sf++;
		x2 = x2 + BASICX * shrinkfactor[sf];
		y2 = y2 + BASICY * shrinkfactor[sf];
	}

	/* draw top of right wall */
	x = playerx;
	y = playery;
	x1 = SCREEN_WIDTH;
	y1 = 0;
	sf = 0;
	x2 = x1 - BASICX * shrinkfactor[sf];
	y2 = y1 + BASICY * shrinkfactor[sf];
	right = playerdir + 1;
	if (right > 3)
		right = 0;	
	for (i = 0; i < steps; i++) {
		if (!inbounds(x + xo[right], y + yo[right], xdim, ydim))
			continue;
		if (maze[(y + yo[right]) * xdim + x + xo[right]] == '.') {
			olLine(x1, y1, x2, y2, wallcolor);
		} else {
			olLine(x1, y2, x2, y2, wallcolor);
			olLine(x1, SCREEN_HEIGHT - y2, x2,
				SCREEN_HEIGHT - y2, wallcolor);
			olLine(x2, y2, x2, SCREEN_HEIGHT - y2, wallcolor);
			olLine(x1, y1, x1, SCREEN_HEIGHT - y1, wallcolor);
		}
		x += xo[playerdir];
		y += yo[playerdir];
		if (maze[y * xdim + x] == '.') /* back wall */
			break;
		x1 = x2;
		y1 = y2;
		sf++;
		x2 = x2 - BASICX * shrinkfactor[sf];
		y2 = y2 + BASICY * shrinkfactor[sf];
	}

	/* draw the bottom of left wall */
	x = playerx;
	y = playery;
	x1 = 0;
	y1 = SCREEN_HEIGHT;
	sf = 0;
	x2 = x1 + BASICX * shrinkfactor[sf];
	y2 = y1 - BASICY * shrinkfactor[sf];
	left = playerdir - 1;
	if (left < 0)
		left = 3;	
	for (i = 0; i < steps; i++) {
		if (!inbounds(x + xo[left], y + yo[left], xdim, ydim))
			continue;
		if (maze[(y + yo[left]) * xdim + x + xo[left]] == '.')
			olLine(x1, y1, x2, y2, wallcolor);
		x += xo[playerdir];
		y += yo[playerdir];
		if (maze[y * xdim + x] == '.') /* back wall */
			break;
		x1 = x2;
		y1 = y2;
		sf++;
		x2 = x2 + BASICX * shrinkfactor[sf];
		y2 = y2 - BASICY * shrinkfactor[sf];
	}

	/* draw bottom of right wall */
	x = playerx;
	y = playery;
	x1 = SCREEN_WIDTH;
	y1 = SCREEN_HEIGHT;
	sf = 0;
	x2 = x1 - BASICX * shrinkfactor[sf];
	y2 = y1 - BASICY * shrinkfactor[sf];
	right = playerdir + 1;
	if (right > 3)
		right = 0;	
	for (i = 0; i < steps; i++) {
		if (!inbounds(x + xo[right], y + yo[right], xdim, ydim))
			continue;
		if (maze[(y + yo[right]) * xdim + x + xo[right]] == '.')
			olLine(x1, y1, x2, y2, wallcolor);
		x += xo[playerdir];
		y += yo[playerdir];
		if (maze[y * xdim + x] == '.') /* back wall */
			break;
		x1 = x2;
		y1 = y2;
		sf++;
		x2 = x2 - BASICX * shrinkfactor[sf];
		y2 = y2 - BASICY * shrinkfactor[sf];
	}
}

static void openlase_renderframe(float *elapsed_time)
{
	*elapsed_time = olRenderFrame(60);
	olLoadIdentity();
	olTranslate(-1,1);
	olScale(XSCALE, YSCALE);
}

static void deal_with_joystick(void)
{
	static struct wwvi_js_event jse;
	int *xaxis, *yaxis, rc, i;

	if (joystick_fd < 0)
		return;

	xaxis = &jse.stick_x;
        yaxis = &jse.stick_y;

	memset(&jse.button[0], 0, sizeof(jse.button[0]*10));
	rc = get_joystick_status(&jse);
	if (rc != 0)
		return;

#define JOYSTICK_SENSITIVITY 5000
#define XJOYSTICK_THRESHOLD 20000
#define YJOYSTICK_THRESHOLD 20000

	/* check joystick buttons */

	for (i = 0; i < 11; i++) {
		if (jse.button[i] == 1) {
			attract_mode_active = 0;
		}
	}

	if (jse.button[0] == 1) {
		requested_button_zero = 1;
	} else {
		requested_button_zero = 0;
	}

	if (*xaxis < -XJOYSTICK_THRESHOLD)
		requested_left = 1;
	else
		requested_left = 0;
	if (*xaxis > XJOYSTICK_THRESHOLD)
		requested_right = 1;
	else
		requested_right = 0;
	if (*yaxis < -YJOYSTICK_THRESHOLD)
		requested_forward = 1;
	else
		requested_forward = 0;
	if (*yaxis > YJOYSTICK_THRESHOLD)
		requested_backward = 1;
	else
		requested_backward = 0;
}

static void setup_vects(void)
{
	setup_vect(robot_vect, robot_points);
	setup_vect(up_ladder_vect, up_ladder_points);
	setup_vect(down_ladder_vect, down_ladder_points);
}

static void robot_move(struct object *o, char *maze, float time)
{
	int nx, ny;
	int count = 0;
	static float robot_move_time = 1.0;

	o->time_since_last_move += time;

	if (o->time_since_last_move < robot_move_time)
		return;

	o->time_since_last_move = 0.0;

	do {
		count++;

		if (count > 10) {
			nx = o->x;
			ny = o->y;
			break;
		}

		nx = o->x + xo[o->direction];
		ny = o->y + yo[o->direction];

		if (!inbounds(nx, ny, XDIM, YDIM)) {
			o->direction = randomn(4);
			continue;
		}

		if (maze[ny * XDIM + nx] != '#') {
			o->direction = randomn(4);
			continue;
		}
		break;
	} while (1);
	o->x = nx;
	o->y = ny;
}

static void no_move(__attribute__((unused)) struct object *o,
			__attribute__((unused)) char *maze,
			__attribute__((unused)) float time)
{
	return;
}

static void add_robots(char *maze, int level, int xdim, int ydim, int nrobots)
{
	int i;
	int x, y;
	int r;


	for (i = 0; i < nrobots; i++) {
		do {
			x = randomn(xdim);
			y = randomn(ydim);
		} while (maze[xdim * y + x] != '#');
		r = snis_object_pool_alloc_obj(obj_pool);
		nobjs++;
		o[r].x = x;
		o[r].y = y;
		o[r].level = level;
		o[r].n = r;
		o[r].alive = 1;
		o[r].move = robot_move;
		o[r].draw = draw_generic;
		o[r].v = &robot_vect;
	}
}

static void create_ladder(int x, int y, int level, struct my_vect_obj *v)
{
	int l;

	l = snis_object_pool_alloc_obj(obj_pool);
	nobjs++;
	o[l].x = x;
	o[l].y = y;
	o[l].level = level;
	o[l].n = l;
	o[l].alive = 1;
	o[l].move = no_move;
	o[l].draw = draw_generic;
	o[l].v = v;
}

static void create_up_ladder(int x, int y, int level)
{
	create_ladder(x, y, level, &up_ladder_vect);
}

static void create_down_ladder(int x, int y, int level)
{
	create_ladder(x, y, level, &down_ladder_vect);
}

static void add_ladders(char *uppermaze, char *lowermaze, int lowerlevel, int xdim, int ydim)
{
	int i, j, x, y;
	int down, up;
	int bad_spot;

	for (i = 0; i < LADDERS_BETWEEN_LEVELS; i++) {
		do {
			x = randomn(xdim);
			y = randomn(ydim) ;
		} while (uppermaze[xdim * y + x] != '#' ||
			lowermaze[xdim * y + x] != '#');

		bad_spot = 0;
		for (j = 0; j < nobjs; j++) {
			if (x == o[j].x && y == o[j].y &&
				(o[j].level == lowerlevel || 
				o[j].level == lowerlevel - 1)) {
				bad_spot = 1;
				break;
			}
		}
		if (bad_spot)
			continue;
		create_up_ladder(x, y, lowerlevel);
		create_down_ladder(x, y, lowerlevel - 1); 
	}
}

int main(int argc, char *argv[])
{
	char *maze[MAXLEVELS];
	struct timeval tv;
	float elapsed_time = 0.0;
	int xdim = XDIM;
	int ydim = YDIM;
	int i;

	init_shrinkfactor(NSTEPS);
	setup_vects();

	gettimeofday(&tv, NULL);
	srand(tv.tv_usec);

	snis_object_pool_setup(&obj_pool, MAXOBJS);

	joystick_fd = open_joystick(JOYSTICK_DEVICE, NULL);
	if (joystick_fd < 0)
		printf("No joystick...");


	playerx = xdim / 2;
	playery = ydim - 2;
	playerdir = 0;
	playerlevel = 0;
	for (i = 0; i < MAXLEVELS; i++) {
		maze[i] = make_maze(xdim, ydim, playerx, playery, playerdir);
		print_maze(maze[i], xdim, ydim);
		printf("density = %f\n", maze_density(maze[i], xdim, ydim));
		add_robots(maze[i], i, xdim, ydim, nrobots);
	}

	for (i = 0; i < MAXLEVELS - 1; i++)
		add_ladders(maze[i], maze[i + 1], i + 1, xdim, ydim);

	if (setup_openlase())
		return -1;

	for (;;) {
		deal_with_joystick();
		draw_maze(maze[playerlevel], xdim, ydim, playerx, playery, playerdir);
		draw_objects(maze[playerlevel], xdim, ydim);
		attract_mode();
		openlase_renderframe(&elapsed_time);
		move_objects(maze[playerlevel], xdim, ydim, elapsed_time);
	}
	olShutdown();
	return 0;
}
