#if (!defined SDLTILES)

#include "game.h"
/* Explosion Animation */
void game::draw_explosion(int x, int y, int radius, nc_color col)
{
    timespec ts;    // Timespec for the animation of the explosion
    ts.tv_sec = 0;
    ts.tv_nsec = EXPLOSION_SPEED;
    for (int i = 1; i <= radius; i++) {
        mvwputch(w_terrain, y - i + VIEWY - u.posy - u.view_offset_y,
                      x - i + VIEWX - u.posx - u.view_offset_x, col, '/');
        mvwputch(w_terrain, y - i + VIEWY - u.posy - u.view_offset_y,
                      x + i + VIEWX - u.posx - u.view_offset_x, col,'\\');
        mvwputch(w_terrain, y + i + VIEWY - u.posy - u.view_offset_y,
                      x - i + VIEWX - u.posx - u.view_offset_x, col,'\\');
        mvwputch(w_terrain, y + i + VIEWY - u.posy - u.view_offset_y,
                      x + i + VIEWX - u.posx - u.view_offset_x, col, '/');
        for (int j = 1 - i; j < 0 + i; j++) {
            mvwputch(w_terrain, y - i + VIEWY - u.posy - u.view_offset_y,
                       x + j + VIEWX - u.posx - u.view_offset_x, col,'-');
            mvwputch(w_terrain, y + i + VIEWY - u.posy - u.view_offset_y,
                       x + j + VIEWX - u.posx - u.view_offset_x, col,'-');
            mvwputch(w_terrain, y + j + VIEWY - u.posy - u.view_offset_y,
                       x - i + VIEWX - u.posx - u.view_offset_x, col,'|');
            mvwputch(w_terrain, y + j + VIEWY - u.posy - u.view_offset_y,
                       x + i + VIEWX - u.posx - u.view_offset_x, col,'|');
        }
        wrefresh(w_terrain);
        nanosleep(&ts, NULL);
    }
}
/* Bullet Animation */
void game::draw_bullet(player &p, int tx, int ty, int i, std::vector<point> trajectory, char bullet, timespec &ts)
{
    if (u_see(tx, ty)) {
        if (i > 0)
        {
            m.drawsq(w_terrain, u, trajectory[i-1].x, trajectory[i-1].y, false,
                     true, u.posx + u.view_offset_x, u.posy + u.view_offset_y);
        }
        /*
        char bullet = '*';
        if (is_aflame)
         bullet = '#';
        */
        mvwputch(w_terrain, ty + VIEWY - u.posy - u.view_offset_y,
                 tx + VIEWX - u.posx - u.view_offset_x, c_red, bullet);
        wrefresh(w_terrain);
        if (&p == &u)
         nanosleep(&ts, NULL);
   }
}
/* Monster hit animation */
void game::draw_hit_mon(int x, int y, monster m, bool dead)
{
    /*
    x - u.posx + VIEWX - u.view_offset_x,
                y - u.posy + VIEWY - u.view_offset_y,
    */
    nc_color cMonColor = m.type->color;
    char sMonSym = m.symbol();

    hit_animation(x + VIEWX - u.posx - u.view_offset_x,
                  y + VIEWY - u.posy - u.view_offset_y,
                  red_background(cMonColor), dead?'%':sMonSym);
}
/* Player hit animation */
void game::draw_hit_player(player *p, bool dead)
{
    hit_animation(p->posx + VIEWX - u.posx - u.view_offset_x,
                  p->posy + VIEWY - u.posy - u.view_offset_y,
                  red_background(p->color()), '@');
}

#endif
