/*
 * ddk.cc
 *
 * Aikido Language System,
 * export version: 1.00
 * Copyright (c) 2002-2003 Sun Microsystems, Inc. 2003
 *
 * Sun Public License Notice
 * 
 * The contents of this file are subject to the Sun Public License Version 1.0 (the "License"). You
 * may not use this file except in compliance with the License. A copy of the License is available
 * at http://www.opensource.org/licenses/sunpublic.php
 * 
 * The Original Code is Aikido. 
 * The Initial Developer of the Original Code is David Allison on behalf of Sun Microsystems, Inc. 
 * Copyright (C) Sun Microsystems, Inc. 2000-2003. All Rights Reserved.
 * 
 * 
 * Contributor(s): dallison
 *
 * Version:  1.3
 * Created by dallison on 4/19/2002
 * Last modified by dallison on 03/07/29
 */


#include "aikido.h"
#include "gtk/gtk.h"
#include "gdk/gdk.h"
#include "aikidointerpret.h"
#if !defined (_OS_MACOSX)
#include <alloca.h>
#endif

namespace aikido {

extern "C" {


AIKIDO_NATIVE (gdk_window_get_pointer) {
    int x = 0, y = 0 ;
    GdkModifierType type = GDK_SHIFT_MASK ;		// whatever
    gdk_window_get_pointer ((GdkWindow*)paras[1].integer, &x, &y, &type) ;
    *paras[2].addr = (INTEGER)x ;
    *paras[3].addr = (INTEGER)y ;
    *paras[4].addr = (INTEGER)type ;
    return 0 ;
}

const char **make_xpm_data (const Value &v) {
    int width, height, ncols ;
    const char *infoline = (*v.vec)[0].str->c_str() ;
    sscanf (infoline, "%d %d %d", &width, &height, &ncols) ;
    int len = height + ncols + 1 ;
    const char **data = new const char * [len] ;
    for (int i = 0 ; i < len ; i++) {
        data[i] = (*v.vec)[i].str->c_str() ;
    }
    return data ;
}

AIKIDO_NATIVE (gdk_pixmap_create_from_xpm) {
    GdkBitmap *mask ;
    INTEGER r = (INTEGER)gdk_pixmap_create_from_xpm((GdkWindow*)paras[1].integer, &mask, (GdkColor*)paras[3].integer, paras[4].str->c_str()) ;
    *paras[2].addr = (INTEGER)mask ;
    return r ;
}

AIKIDO_NATIVE (gdk_pixmap_colormap_create_from_xpm) {
    GdkBitmap *mask ;
    INTEGER r = (INTEGER)gdk_pixmap_colormap_create_from_xpm((GdkWindow*)paras[1].integer, (GdkColormap*)paras[2].integer, &mask, (GdkColor*)paras[4].integer, paras[5].str->c_str()) ;
    *paras[3].addr = (INTEGER)mask ;
    return r ;
}

AIKIDO_NATIVE (gdk_pixmap_create_from_xpm_d) {
    const char **data = make_xpm_data (paras[4]) ;
    GdkBitmap *mask ;
    INTEGER r = (INTEGER)gdk_pixmap_create_from_xpm_d((GdkWindow*)paras[1].integer, &mask, (GdkColor*)paras[3].integer, (char**)data) ;
    *paras[2].addr = (INTEGER)mask ;
    delete [] data ;
    return r ;
}

AIKIDO_NATIVE (gdk_pixmap_colormap_create_from_xpm_d) {
    const char **data = make_xpm_data (paras[5]) ;
    GdkBitmap *mask ;
    INTEGER r = (INTEGER)gdk_pixmap_colormap_create_from_xpm_d((GdkWindow*)paras[1].integer, (GdkColormap*)paras[2].integer, &mask, (GdkColor*)paras[4].integer, (char**)data) ;
    *paras[3].addr = (INTEGER)mask ;
    delete [] data ;
    return r ;
}


}


}

