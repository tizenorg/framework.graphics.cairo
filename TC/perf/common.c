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

void clearCairo(cairo_t *cr, double width, double height)
{
    cairo_set_source_rgba(cr, 1, 1, 1, 1);
    cairo_rectangle(cr, 0.0, 0.0, width, height);
    cairo_fill(cr);
}

void cairoSquare(cairo_t *cr, double x, double y, double length)
{
    cairo_rectangle(cr, x, y, length, length);
    cairo_fill(cr);
}

void cairoSquareStroke(cairo_t *cr, double x, double y, double length)
{
    cairo_rectangle(cr, x, y, length, length);
    cairo_stroke(cr);
}

void cairoCircle(cairo_t *cr, double x, double y, double radius)
{
    cairo_arc(cr, x, y, radius, 0.0, 2.0 * M_PI);
    cairo_fill(cr);
}

void cairoCircleStroke(cairo_t *cr, double x, double y, double radius)
{
    cairo_arc(cr, x, y, radius, 0.0, 2.0 * M_PI);
    cairo_stroke(cr);
}

void cairoTriangle(cairo_t *cr, double x, double y, double side)
{
    cairo_move_to(cr, x, y);
    cairo_line_to(cr, x + side, y + side);
    cairo_line_to(cr, x, y + side);
    cairo_close_path(cr);
    cairo_fill(cr);
}

void cairoTriangleStroke(cairo_t *cr, double x, double y, double side)
{
    cairo_move_to(cr, x, y);
    cairo_line_to(cr, x + side, y);
    cairo_line_to(cr, x, y + side);
    cairo_close_path(cr);
    cairo_stroke(cr);
}
