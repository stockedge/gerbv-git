/*
 * gEDA - GNU Electronic Design Automation
 * This file is a part of gerbv.
 *
 *   Copyright (C) 2000-2003 Stefan Petersen (spe@stacken.kth.se)
 *
 * $Id$
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111 USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <math.h>  /* ceil(), atan2() */

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include <gtk/gtk.h>

#include "draw.h"
#include "draw_amacro.h"
#include "gerb_error.h"
#include "gerb_transf.h"

#undef round
#define round(x) ceil((double)(x))


/*
 * Draws a circle _centered_ at x,y with diameter dia
 */
static void 
gerbv_draw_circle(GdkPixmap *pixmap, GdkGC *gc, 
		  gint filled, gint x, gint y, gint dia)
{
    static const gint full_circle = 23360;
    gint real_x = x - dia / 2;
    gint real_y = y - dia / 2;
    
    gdk_draw_arc(pixmap, gc, filled, real_x, real_y, dia, dia, 0, full_circle);
    
    return;
} /* gerbv_draw_circle */


/*
 * Draws a rectangle _centered_ at x,y with sides x_side, y_side
 */
static void 
gerbv_draw_rectangle(GdkPixmap *pixmap, GdkGC *gc, 
		     gint filled, gint x, gint y, gint x_side, gint y_side)
{
    
    gint real_x = x - x_side / 2;
    gint real_y = y - y_side / 2;
    
    gdk_draw_rectangle(pixmap, gc, filled, real_x, real_y, x_side, y_side);
    
    return;
} /* gerbv_draw_rectangle */


/*
 * Draws an oval _centered_ at x,y with x axis x_axis and y axis y_axis
 */ 
static void
gerbv_draw_oval(GdkPixmap *pixmap, GdkGC *gc, 
		gint filled, gint x, gint y, gint x_axis, gint y_axis)
{
    gint delta = 0;
    GdkGC *local_gc = gdk_gc_new(pixmap);

    gdk_gc_copy(local_gc, gc);

    if (x_axis > y_axis) {
	/* Draw in x axis */
	delta = x_axis / 2 - y_axis / 2;
	gdk_gc_set_line_attributes(local_gc, y_axis, 
				   GDK_LINE_SOLID, 
				   GDK_CAP_ROUND, 
				   GDK_JOIN_MITER);
	gdk_draw_line(pixmap, local_gc, x - delta, y, x + delta, y);
    } else {
	/* Draw in y axis */
	delta = y_axis / 2 - x_axis / 2;
	gdk_gc_set_line_attributes(local_gc, x_axis, 
				   GDK_LINE_SOLID, 
				   GDK_CAP_ROUND, 
				   GDK_JOIN_MITER);
	gdk_draw_line(pixmap, local_gc, x, y - delta, x, y + delta);
    }

    gdk_gc_unref(local_gc);

    return;
} /* gerbv_draw_oval */


/*
 * Draws an arc 
 * Draws an arc _centered_ at x,y
 * direction:  0 counterclockwise, 1 clockwise
 */
static void
gerbv_draw_arc(GdkPixmap *pixmap, GdkGC *gc,
	       int x, int y,
	       int width, int height,
	       double angle1, double angle2)
{
    gint real_x = x - width / 2;
    gint real_y = y - height / 2;

    gdk_draw_arc(pixmap, gc, FALSE, real_x, real_y, width, height, 
		 (gint)angle1 * 64.0, (gint)(angle2 - angle1) * 64.0);
    
    return;
} /* gerbv_draw_arc */


/*
 * Convert a gerber image to a GDK clip mask to be used when creating pixmap
 */
int
image2pixmap(GdkPixmap **pixmap, struct gerb_image *image, 
	     gerb_transf_t *transf, enum polarity_t polarity)
{
    GdkGC *gc = gdk_gc_new(*pixmap);
    GdkGC *pgc = gdk_gc_new(*pixmap);
    struct gerb_net *net;
    gint x1, y1, x2, y2;
    int p1, p2;
    int cir_width = 0, cir_height = 0;
    int cp_x = 0, cp_y = 0;
    GdkPoint *points = NULL;
    int curr_point_idx = 0;
    int in_parea_fill = 0;
    GdkColor transparent, opaque;
    gerb_transf_t trf;


    if (image == NULL || image->netlist == NULL) {
	/*
	 * Destroy GCs before exiting
	 */
	gdk_gc_unref(gc);
	gdk_gc_unref(pgc);
	
	return 0;
    }
    
    /*TODO effects of both:combine global transf with image->transf*/
    trf = *transf;
    
    
    /* Set up the two "colors" we have */
    opaque.pixel = 0; /* opaque will not let color through */
    transparent.pixel = 1; /* transparent will let color through */ 

    /*
     * Clear clipmask and set draw color depending image on image polarity
     */
    if (polarity == NEGATIVE) {
	gdk_gc_set_foreground(gc, &transparent);
	gdk_draw_rectangle(*pixmap, gc, TRUE, 0, 0, -1, -1);
	gdk_gc_set_foreground(gc, &opaque);
    } else {
	gdk_gc_set_foreground(gc, &opaque);
	gdk_draw_rectangle(*pixmap, gc, TRUE, 0, 0, -1, -1);
	gdk_gc_set_foreground(gc, &transparent);
    }

    for (net = image->netlist->next ; net != NULL; net = net->next) {
        double x1d, y1d, x2d, y2d;
        double x, y; /*for circle centre*/
        int x_stop, y_stop; /* not transformed x2,y2 */
        trf.scale = transf->scale;
        if (net->unit == MM) 
            trf.scale /= 25.4;

	/*
	 * Scale points with window scaling and translate them
	 */
        gerb_transf_apply(image->info->offset_a + net->start_x,image->info->offset_b - net->start_y,
            &trf, &x1d, &y1d);
        gerb_transf_apply(image->info->offset_a + net->stop_x,image->info->offset_b - net->stop_y,
            &trf, &x2d, &y2d);    
        x1 = (int)round(x1d);
        y1 = (int)round(y1d);
        x2 = (int)round(x2d);/*CHECK ME: Why integer cast?*/
        y2 = (int)round(y2d);
        x_stop = (int)round(image->info->offset_a + net->stop_x);
        y_stop = (int)round(image->info->offset_b - net->stop_y);

	/* 
	 * If circle segment, scale and translate that one too
	 */
	if (net->cirseg) {
	    cir_width = (int)round(net->cirseg->width * trf.scale); /*CHECK ME later: take into account rotation and other matrix trafos*/
	    cir_height = (int)round(net->cirseg->height * trf.scale);
            gerb_transf_apply(image->info->offset_a + net->cirseg->cp_x,image->info->offset_b - net->cirseg->cp_y,
            &trf, &x, &y);  
	    cp_x = (int)round(x);
	    cp_y = (int)round(y);
            /* FIXME recalculate angles as well */
	}

	/*
	 * Set GdkFunction depending on if this (gerber) layer is inverted
	 * and allow for the photoplot being negative.
	 */
	gdk_gc_set_function(gc, GDK_COPY);
	if ((net->layer_polarity == CLEAR) != (polarity == NEGATIVE))
	    gdk_gc_set_foreground(gc, &opaque);
	else
	    gdk_gc_set_foreground(gc, &transparent);

	/*
	 * Polygon Area Fill routines
	 */
	switch (net->interpolation) {
	case PAREA_START :
	    points = (GdkPoint *)malloc(sizeof(GdkPoint) *  net->nuf_pcorners);
	    if (points == NULL) {
		GERB_FATAL_ERROR("Malloc failed\n");
	    }
	    memset(points, 0, sizeof(GdkPoint) *  net->nuf_pcorners);
	    curr_point_idx = 0;
	    in_parea_fill = 1;
	    continue;
	case PAREA_END :
	    gdk_gc_copy(pgc, gc); 
	    gdk_gc_set_line_attributes(pgc, 1, 
				       GDK_LINE_SOLID, 
				       GDK_CAP_PROJECTING, 
				       GDK_JOIN_MITER);
	    gdk_draw_polygon(*pixmap, pgc, 1, points, curr_point_idx);
	    free(points);
	    points = NULL;
	    in_parea_fill = 0;
	    continue;
	default :
	    break;
	}

	if (in_parea_fill) {
	    points[curr_point_idx].x = x2;
	    points[curr_point_idx].y = y2;
	    curr_point_idx++;
	    continue;
	}

	/*
	 * If aperture state is off we allow use of undefined apertures.
	 * This happens when gerber files starts, but hasn't decided on 
	 * which aperture to use.
	 */
	if (image->aperture[net->aperture] == NULL) {
	    if (net->aperture_state != OFF)
		GERB_MESSAGE("Aperture [%d] is not defined\n", net->aperture);
	    continue;
	}

	/*
	 * "Normal" aperture drawing routines
	 */
        trf.scale = transf->scale; /*TODO must be  changed once image->transf and transf are combined*/
	if (image->aperture[net->aperture]->unit == MM)
	    trf.scale /=  25.4;


	switch (net->aperture_state) {
	case ON :
	    p1 = (int)round(image->aperture[net->aperture]->parameter[0] * trf.scale);
	    if (image->aperture[net->aperture]->type == RECTANGLE)
		gdk_gc_set_line_attributes(gc, p1, 
					   GDK_LINE_SOLID, 
					   GDK_CAP_PROJECTING, 
					   GDK_JOIN_MITER);
	    else
		gdk_gc_set_line_attributes(gc, p1, 
					   GDK_LINE_SOLID, 
					   GDK_CAP_ROUND, 
					   GDK_JOIN_MITER);

	    switch (net->interpolation) {
	    case LINEARx10 :
	    case LINEARx01 :
	    case LINEARx001 :
		GERB_MESSAGE("Linear != x1\n");
		gdk_gc_set_line_attributes(gc, p1, 
					   GDK_LINE_ON_OFF_DASH, 
					   GDK_CAP_ROUND, 
					   GDK_JOIN_MITER);
		gdk_draw_line(*pixmap, gc, x1, y1, x2, y2);
		gdk_gc_set_line_attributes(gc, p1, 
					   GDK_LINE_SOLID,
					   GDK_CAP_ROUND, 
					   GDK_JOIN_MITER);
		break;
	    case LINEARx1 :
		gdk_draw_line(*pixmap, gc, x1, y1, x2, y2);
		break;
	    case CW_CIRCULAR :
	    case CCW_CIRCULAR :
                /* FIXME: angles have to be transformed (rotated)! */
		gerbv_draw_arc(*pixmap, gc, cp_x, cp_y, cir_width, cir_height, 
			       net->cirseg->angle1, net->cirseg->angle2);
		break;		
	    default :
		break;
	    }
	    break;
	case OFF :
	    break;
	case FLASH :
	    p1 = (int)round(image->aperture[net->aperture]->parameter[0] * trf.scale);
	    p2 = (int)round(image->aperture[net->aperture]->parameter[1] * trf.scale);
	    
	    switch (image->aperture[net->aperture]->type) {
	    case CIRCLE :
		gerbv_draw_circle(*pixmap, gc, TRUE, x2, y2, p1);
		break;
	    case RECTANGLE:
		gerbv_draw_rectangle(*pixmap, gc, TRUE, x2, y2, p1, p2);
		break;
	    case OVAL :
		gerbv_draw_oval(*pixmap, gc, TRUE, x2, y2, p1, p2);
		break;
	    case POLYGON :
		GERB_COMPILE_WARNING("Very bad at drawing polygons.\n");
		gerbv_draw_circle(*pixmap, gc, TRUE, x2, y2, p1);
		break;
	    case MACRO :
		gerbv_draw_amacro(*pixmap, gc, 
				  image->aperture[net->aperture]->amacro->program,
				  image->aperture[net->aperture]->amacro->nuf_push,
				  image->aperture[net->aperture]->parameter,
				  &trf, x2, y2);
		break;
	    default :
		GERB_MESSAGE("Unknown aperture type\n");
		return 0;
	    }
	    break;
	default :
	    GERB_MESSAGE("Unknown aperture state\n");
	    return 0;
	}
    }

    /*
     * Destroy GCs before exiting
     */
    gdk_gc_unref(gc);
    gdk_gc_unref(pgc);
    
    return 1;

} /* image2pixmap */
