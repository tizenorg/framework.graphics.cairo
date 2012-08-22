/*
 * Cairo Performance Test Framework
 * (c) 2012 Samsung Electronics, Inc.
 * All rights reserved.
 *
 * Measures rendering performance for image, gl backends
 *
 * This software is a confidential and proprietary information of Samsung
 * Electronics, Inc. ("Confidential Information"). You shall not disclose such
 * Confidential Information and shall use it only in accordance with the terms
 * of the license agreement you entered into with Samsung Electronics.
 *
 * Author: Dongyeon Kim <dy5.kim@samsung.com>
 */

#include "common.h"

#define RENDER_LOOP 100

extern int WIDTH, HEIGHT;

int preRender(cairo_t *cr)
{
    return 1;
}

int render(cairo_t *cr)
{
    int i;
    double r, g, b, a;

    clearCairo(cr, WIDTH, HEIGHT);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    for(i = 0; i < RENDER_LOOP; i++)
    {
        r = drand48();
        g = drand48();
        b = drand48();
        a = drand48();
        float x = drand48() * WIDTH;
        float y = drand48() * HEIGHT;
        float side = drand48() * 300;
        int shape = drand48() * 3;

        cairo_set_source_rgba(cr, r, g, b, a);

        if(shape == 0)
            cairoSquare(cr, x, y, side);
        else if(shape == 1)
            cairoCircle(cr, x, y, side/2);
        else
            cairoTriangle(cr, x, y, side);
    }

    return 1;
}

int postRender(cairo_t *cr)
{
    return 1;
}

