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

extern cairo_device_t *cairo_device;
cairo_pattern_t *pattern1, *pattern2;
int image_width, image_height;

int preRender(cairo_t *cr)
{
    { // Image 1
        cairo_surface_t *image_surface = cairo_image_surface_create_from_png("./image1.png");
        image_width = cairo_image_surface_get_width(image_surface);
        image_height = cairo_image_surface_get_height(image_surface);

        if(cairo_surface_get_type(cairo_get_target(cr)) == CAIRO_SURFACE_TYPE_IMAGE) {
            pattern1 = cairo_pattern_create_for_surface(image_surface);
        } else {
            cairo_surface_t *gl_surface = cairo_gl_surface_create(cairo_device, CAIRO_CONTENT_COLOR_ALPHA,
                        image_width, image_height);
            cairo_t *cr_gl = cairo_create(gl_surface);
            cairo_set_source_surface(cr_gl, image_surface, 0, 0);
            cairo_paint(cr_gl);

            pattern1 = cairo_pattern_create_for_surface(gl_surface);

            cairo_surface_destroy(gl_surface);
            cairo_destroy(cr_gl);
        }
        cairo_surface_destroy(image_surface);
    }
    { // Image 2
        cairo_surface_t *image_surface = cairo_image_surface_create_from_png("./image2.png");
        image_width = cairo_image_surface_get_width(image_surface);
        image_height = cairo_image_surface_get_height(image_surface);

        if(cairo_surface_get_type(cairo_get_target(cr)) == CAIRO_SURFACE_TYPE_IMAGE) {
            pattern2 = cairo_pattern_create_for_surface(image_surface);
        } else {
            cairo_surface_t *gl_surface = cairo_gl_surface_create(cairo_device, CAIRO_CONTENT_COLOR_ALPHA,
                        image_width, image_height);
            cairo_t *cr_gl = cairo_create(gl_surface);
            cairo_set_source_surface(cr_gl, image_surface, 0, 0);
            cairo_paint(cr_gl);

            pattern2 = cairo_pattern_create_for_surface(gl_surface);

            cairo_surface_destroy(gl_surface);
            cairo_destroy(cr_gl);
        }
        cairo_surface_destroy(image_surface);
    }

    return 1;
}

int render(cairo_t *cr)
{
    int i;

    clearCairo(cr, WIDTH, HEIGHT);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    for(i = 0; i < RENDER_LOOP; i++)
    {
        float x = drand48() * WIDTH - image_width / 2;
        float y = drand48() * HEIGHT - image_height / 2;
        int index = drand48() * 2;

        cairo_identity_matrix(cr);
        cairo_translate(cr, x, y);
        if(index == 0)
            cairo_set_source(cr, pattern1);
        else
            cairo_set_source(cr, pattern2);
        cairoSquare(cr, 0, 0, image_width);
    }

    return 1;
}

int postRender(cairo_t *cr)
{
    cairo_pattern_destroy(pattern1);
    cairo_pattern_destroy(pattern2);
    return 1;
}

