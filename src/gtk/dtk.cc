/*
 * dtk.cc
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
 * Closure support is Copyright (C) 2004 David Allison.  All rights reserved.
 * 
 * Contributor(s): dallison
 *
 * Version:  1.4
 * Created by dallison on 4/19/2002
 * Last modified by dallison on 03/07/29
 */


// CHANGE LOG
// 10/21/2004   David Allison           Added use of closures

#include "aikido.h"
#include "gtk/gtk.h"
#include "gdk/gdk.h"
#include "aikidointerpret.h"
#if !defined (_OS_MACOSX)
#include <alloca.h>
#endif

namespace aikido {


extern bool isIntegral (const Value &v) ;
extern INTEGER getInt (const Value &v) ;

extern "C" {
StackFrame *mainStack ;
StackFrame *GDKStack ;
StackFrame *GTKStack ;
StaticLink mainSlink ;
StaticLink GDKSlink ;
StaticLink GTKSlink ;

Package *gdk ;
Package *gtk ;
Class *EventAnyClass ;
Class *EventExposeClass ;
Class *EventNoExposeClass ;
Class *EventVisibilityClass ;
Class *EventMotionClass ;
Class *EventButtonClass ;
Class *EventKeyClass ;
Class *EventCrossingClass ;
Class *EventFocusClass ;
Class *EventConfigureClass ;
Class *EventPropertyClass ;
Class *EventSelectionClass ;
Class *EventProximityClass ;
Class *EventClientClass ;
Class *EventDNDClass ;


static Class *findEventClass (const string &name) {
    Scope *scope ;
    Tag *tag = (Tag*)gdk->findVariable (name, scope, VAR_ACCESSFULL, NULL, NULL) ;
    if (tag == NULL) {
        throw Exception ("Can't find event class") ;
    }
    return (Class*)tag->block ;
}

static Class *findObjectClass (const string &name) {
    Scope *scope ;
    Tag *tag = (Tag*)gtk->findVariable (name, scope, VAR_ACCESSFULL, NULL, NULL) ;
    if (tag == NULL) {
        throw Exception ("Can't find GTK object class") ;
    }
    return (Class*)tag->block ;
}

void initsignals() ;
//void initObjectRegistry() ;

AIKIDO_NATIVE (aikido_gtk_init) {
    int argc ;
    char **argv = new char *[30] ;
    argv[0] = (char*)"dtk" ;

    Value::vector *args = paras[1].vec ;
    argc = args->size() + 1;
    for (int i = 0 ; i < args->size() ; i++) {
        argv[i+1] = (char*)(*args)[i].str->c_str() ;
    }
    gtk_init (&argc, &argv) ;

    initsignals() ;
    //initObjectRegistry() ;

    mainStack = b->mainStack ;
    mainSlink = StaticLink (&mainSlink, mainStack) ;
    GDKSlink = *staticLink ;
    GTKSlink = *staticLink ;

    GDKStack = GDKSlink.frame ;		// XXX: this is not right
    GTKStack = GTKSlink.frame ;
    Tag *g = b->findTag (string (".GDK")) ;
    gdk = (Package*)g->block ;
    g = b->findTag (string (".GTK")) ;
    gtk = (Package*)g->block ;

    EventAnyClass = findEventClass (".EventAny") ;
    EventExposeClass = findEventClass (".EventExpose") ;
    EventNoExposeClass = findEventClass (".EventNoExpose") ;
    EventVisibilityClass = findEventClass (".EventVisibility") ;
    EventMotionClass = findEventClass (".EventMotion") ;
    EventButtonClass = findEventClass (".EventButton") ;
    EventKeyClass = findEventClass (".EventKey") ;
    EventCrossingClass = findEventClass (".EventCrossing") ;
    EventFocusClass = findEventClass (".EventFocus") ;
    EventConfigureClass = findEventClass (".EventConfigure") ;
    EventPropertyClass = findEventClass (".EventProperty") ;
    EventSelectionClass = findEventClass (".EventSelection") ;
    EventProximityClass = findEventClass (".EventProximity") ;
    EventClientClass = findEventClass (".EventClient") ;
    EventDNDClass = findEventClass (".EventDND") ;
    return 0 ;
}

Object *constructEvent (Class *cls, GdkEvent *event, Object *window, VirtualMachine *vm) {
    Object *obj = NULL ;
    obj = new (cls->stacksize) Object (cls, &GDKSlink, GDKStack, GDKStack->inst) ;
    obj->ref++ ;
    obj->varstack[0] = obj ;
    vm->execute (cls, obj, &GDKSlink, 0) ;
    obj->varstack[1] = (INTEGER)event->type ;
    obj->varstack[2] = (INTEGER)event->any.window ;
    obj->varstack[3] = (INTEGER)event->any.send_event ;
    obj->ref-- ;
    return obj ;
}

Value makeEvent (GdkEvent *event, VirtualMachine *vm, Object *window) {
    Object *obj = NULL ;
    switch (event->type) {
    case GDK_NOTHING           :
        break ;
    case GDK_DELETE            :
    case GDK_DESTROY           :
    case GDK_MAP               :
    case GDK_UNMAP             :
        obj = constructEvent (EventAnyClass, event, window, vm) ;
        break ;
    case GDK_NO_EXPOSE         :
        obj = constructEvent (EventNoExposeClass, event, window, vm) ;
        break ;
    case GDK_EXPOSE            :
        obj = constructEvent (EventExposeClass, event, window, vm) ;
        obj->varstack[4] = (INTEGER)event->expose.area.x;
        obj->varstack[5] = (INTEGER)event->expose.area.y;
        obj->varstack[6] = (INTEGER)event->expose.area.width;
        obj->varstack[7] = (INTEGER)event->expose.area.height;
        obj->varstack[8] = (INTEGER)event->expose.count ;
        break ;
    case GDK_MOTION_NOTIFY     :
    case GDK_DRAG_MOTION       :
        obj = constructEvent (EventMotionClass, event, window, vm) ;
        obj->varstack[4] = (INTEGER)event->motion.time ;
        obj->varstack[5] = (double)event->motion.x ;
        obj->varstack[6] = (double)event->motion.y ;
        obj->varstack[7] = (double)event->motion.pressure ;
        obj->varstack[8] = (double)event->motion.xtilt ;
        obj->varstack[9] = (double)event->motion.ytilt ;
        obj->varstack[10] = (INTEGER)event->motion.state ;
        obj->varstack[11] = (INTEGER)event->motion.is_hint ;
        obj->varstack[12] = (INTEGER)event->motion.source ;
        obj->varstack[13] = (INTEGER)event->motion.deviceid ;
        obj->varstack[14] = (double)event->motion.x_root ;
        obj->varstack[15] = (double)event->motion.y_root ;
        break ;
    case GDK_BUTTON_PRESS      :
    case GDK_2BUTTON_PRESS     :
    case GDK_3BUTTON_PRESS     :
    case GDK_BUTTON_RELEASE    :
        obj = constructEvent (EventButtonClass, event, window, vm) ;
        obj->varstack[4] = (INTEGER)event->button.time ;
        obj->varstack[5] = (double)event->button.x ;
        obj->varstack[6] = (double)event->button.y ;
        obj->varstack[7] = (double)event->button.pressure ;
        obj->varstack[8] = (double)event->button.xtilt ;
        obj->varstack[9] = (double)event->button.ytilt ;
        obj->varstack[10] = (INTEGER)event->button.state ;
        obj->varstack[11] = (INTEGER)event->button.button ;
        obj->varstack[12] = (INTEGER)event->button.source ;
        obj->varstack[13] = (INTEGER)event->button.deviceid ;
        obj->varstack[14] = (double)event->button.x_root ;
        obj->varstack[15] = (double)event->button.y_root ;
        break ;
    case GDK_KEY_PRESS         :
    case GDK_KEY_RELEASE       :
        obj = constructEvent (EventKeyClass, event, window, vm) ;
        obj->varstack[4] = (INTEGER)event->key.time ;
        obj->varstack[5] = (INTEGER)event->key.state ;
        obj->varstack[6] = (INTEGER)event->key.keyval ;
        obj->varstack[7] = (INTEGER)event->key.length ;
        obj->varstack[8] = event->key.string ;
        break ;
    case GDK_ENTER_NOTIFY      :
    case GDK_LEAVE_NOTIFY      :
        obj = constructEvent (EventCrossingClass, event, window, vm) ;
        obj->varstack[4] = (INTEGER)event->crossing.subwindow ;
        obj->varstack[5] = (INTEGER)event->crossing.time ;
        obj->varstack[6] = (double)event->crossing.x ;
        obj->varstack[7] = (double)event->crossing.y ;
        obj->varstack[8] = (double)event->crossing.x_root ;
        obj->varstack[9] = (double)event->crossing.y_root ;
        obj->varstack[10] = (INTEGER)event->crossing.mode ;
        obj->varstack[11] = (INTEGER)event->crossing.detail ;
        obj->varstack[12] = (INTEGER)event->crossing.focus ;
        obj->varstack[13] = (INTEGER)event->crossing.state ;
        break ;
    case GDK_FOCUS_CHANGE      :
        obj = constructEvent (EventFocusClass, event, window, vm) ;
        obj->varstack[4] = (INTEGER)event->focus_change.in ;
        break ;
    case GDK_CONFIGURE         :
        obj = constructEvent (EventConfigureClass, event, window, vm) ;
        obj->varstack[4] = (INTEGER)event->configure.x ;
        obj->varstack[5] = (INTEGER)event->configure.y ;
        obj->varstack[6] = (INTEGER)event->configure.width ;
        obj->varstack[7] = (INTEGER)event->configure.height ;
        break ;
    case GDK_PROPERTY_NOTIFY   :
        obj = constructEvent (EventPropertyClass, event, window, vm) ;
        obj->varstack[4] = (INTEGER)event->property.atom ;
        obj->varstack[5] = (INTEGER)event->property.time ;
        obj->varstack[6] = (INTEGER)event->property.state ;
        break ;
    case GDK_SELECTION_CLEAR   :
    case GDK_SELECTION_REQUEST :
    case GDK_SELECTION_NOTIFY  :
        obj = constructEvent (EventSelectionClass, event, window, vm) ;
        obj->varstack[4] = (INTEGER)event->selection.selection ;
        obj->varstack[5] = (INTEGER)event->selection.target ;
        obj->varstack[6] = (INTEGER)event->selection.property ;
        obj->varstack[7] = (INTEGER)event->selection.requestor ;
        obj->varstack[8] = (INTEGER)event->selection.time ;
        break ;
    case GDK_PROXIMITY_IN      :
    case GDK_PROXIMITY_OUT     :
        obj = constructEvent (EventProximityClass, event, window, vm) ;
        obj->varstack[4] = (INTEGER)event->proximity.time ;
        obj->varstack[5] = (INTEGER)event->proximity.source ;
        obj->varstack[6] = (INTEGER)event->proximity.deviceid ;
        break ;
    case GDK_DRAG_ENTER        :
    case GDK_DRAG_LEAVE        :
    case GDK_DRAG_STATUS       :
    case GDK_DROP_START        :
    case GDK_DROP_FINISHED     :
        obj = constructEvent (EventDNDClass, event, window, vm) ;
        obj->varstack[4] = (INTEGER)event->dnd.context ;		// XXX: create an object here?
        obj->varstack[5] = (INTEGER)event->dnd.time ;
        obj->varstack[6] = (INTEGER)event->dnd.x_root ;
        obj->varstack[7] = (INTEGER)event->dnd.y_root ;
        
        break ;
    case GDK_CLIENT_EVENT      :
        obj = constructEvent (EventClientClass, event, window, vm) ;
        obj->varstack[4] = (INTEGER)event->client.message_type ;
        obj->varstack[5] = (INTEGER)event->client.data_format ;
	// XXX: assign data
        break ;
    case GDK_VISIBILITY_NOTIFY :
        obj = constructEvent (EventVisibilityClass, event, window, vm) ;
        obj->varstack[4] = (INTEGER)event->visibility.state ;
        break ;
    }
    return obj ;
}


Value constructObject (GtkTypeQuery *q, VirtualMachine *vm) {
    std::string name = q->type_name ;
    std::string aikidoname = "." + name.substr (3, name.size() - 3) ;		// remove the Gtk
    Class *cls = findObjectClass (aikidoname) ;

    Object *obj = NULL ;
    obj = new (cls->stacksize) Object (cls, &GTKSlink, GTKStack, GTKStack->inst) ;
    obj->ref++ ;
    obj->varstack[0] = obj ;
    // assign all parameters to value 0
    int nparas = cls->parameters.size() - 1 ;
    for (int i = 0 ; i < nparas ; i++) {
        cls->parameters[i+1]->setValue (Value((INTEGER)0), obj) ;
    }
    // execute the constructor
    vm->execute (cls, obj, &GTKSlink, 0) ;
    obj->ref-- ;
    g_free (q) ;
    return obj ;
}


// the first para is a pointer to an GTKObject.  Using the type information
// in it, build a Aikido object of the appropriate type
AIKIDO_NATIVE (dtk_construct_object) {
    GtkObject *obj = (GtkObject*)paras[1].integer ;
    GtkType type = GTK_OBJECT_TYPE(obj) ;
    GtkTypeQuery *q = gtk_type_query(type) ;
    return constructObject (q, vm) ;
}

struct SignalConnection {
    SignalConnection() {}
    SignalConnection (const Value &obj, GtkObject *native, const string &n, Closure *closure, VirtualMachine *v, StackFrame *s, const Value &a, bool objcall = false) :
        object(obj), name(n), closure(closure), vm(v), stack(mainStack), arg (a), objectcall(objcall) {
        closure->ref++ ;
        thisobj = (Object*)closure->slink->frame ;
    }
    Value object ;
    GtkObject *native ;
    string name ;
    VirtualMachine *vm ;
    StackFrame *stack ;
    Closure *closure ;
    Value arg ;
    int handler ;
    bool objectcall ;		// call using only one parameter
    Object *thisobj ;
    Value call() {
        try {
            Value args[3] ;
            args[0] = thisobj ;
            if (objectcall) {
               args[1] = arg ;
                return vm->call (closure->block, stack, closure->slink, 2, args) ;
            }
            args[1] = object ;
            args[2] = arg ;
            return vm->call (closure->block, stack, closure->slink, 3, args) ;
        } catch (Exception e) {                 // GCC doesn't seem to pass the exceptions through gtk
            vm->aikido->reportException (e) ;
            exit (1) ;
        }
        return 0 ;
    }
    Value call (const Value &p1) {
        Value args[3] ;
        args[0] = thisobj ;
        args[1] = object ;
        args[2] = p1 ;
        try {
            return vm->call (closure->block, stack, closure->slink, 3, args) ;
        } catch (Exception e) {                 // GCC doesn't seem to pass the exceptions through gtk
            vm->aikido->reportException (e) ;
            exit (1) ;
        }
    }
    Value call (const Value &p1, const Value &p2) {
        Value args[4] ;
        args[0] = thisobj ;
        args[1] = object ;
        args[2] = p1 ;
        args[3] = p2 ;
        try {
            return vm->call (closure->block, stack, closure->slink, 4, args) ;
        } catch (Exception e) {                 // GCC doesn't seem to pass the exceptions through gtk
            vm->aikido->reportException (e) ;
            exit (1) ;
        }
    }
    Value call (const Value &p1, const Value &p2, const Value &p3) {
        Value args[5] ;
        args[0] = thisobj ;
        args[1] = object ;
        args[2] = p1 ;
        args[3] = p2 ;
        args[4] = p3 ;
        try {
            return vm->call (closure->block, stack, closure->slink, 5, args) ;
        } catch (Exception e) {                 // GCC doesn't seem to pass the exceptions through gtk
            vm->aikido->reportException (e) ;
            exit (1) ;
        }
    }
    Value call (const Value &p1, const Value &p2, const Value &p3, const Value &p4) {
        Value args[6] ;
        args[0] = thisobj ;
        args[1] = object ;
        args[2] = p1 ;
        args[3] = p2 ;
        args[4] = p3 ;
        args[5] = p4 ;
        try {
            return vm->call (closure->block, stack, closure->slink, 6, args) ;
        } catch (Exception e) {                 // GCC doesn't seem to pass the exceptions through gtk
            vm->aikido->reportException (e) ;
            exit (1) ;
        }
    }
    Value call (const Value &p1, const Value &p2, const Value &p3, const Value &p4, const Value &p5) {
        Value args[7] ;
        args[0] = thisobj ;
        args[1] = object ;
        args[2] = p1 ;
        args[3] = p2 ;
        args[4] = p3 ;
        args[5] = p4 ;
        args[6] = p5 ;
        try {
            return vm->call (closure->block, stack, closure->slink, 7, args) ;
        } catch (Exception e) {                 // GCC doesn't seem to pass the exceptions through gtk
            vm->aikido->reportException (e) ;
            exit (1) ;
        }
    }
    Value call (const Value &p1, const Value &p2, const Value &p3, const Value &p4, const Value &p5, const Value &p6) {
        Value args[8] ;
        args[0] = thisobj ;
        args[1] = object ;
        args[2] = p1 ;
        args[3] = p2 ;
        args[4] = p3 ;
        args[5] = p4 ;
        args[6] = p5 ;
        args[7] = p6 ;
        try {
            return vm->call (closure->block, stack, closure->slink, 8, args) ;
        } catch (Exception e) {                 // GCC doesn't seem to pass the exceptions through gtk
            vm->aikido->reportException (e) ;
            exit (1) ;
        }
    }
    Value callevent(GdkEvent *event) {
        Value args[4] ;
        args[0] = thisobj ;
        args[1] = object ;
        args[2] = makeEvent (event, vm, object.object) ;
        args[3] = arg ;
        try {
            return vm->call (closure->block, stack, closure->slink, 4, args) ;
        } catch (Exception e) {                 // GCC doesn't seem to pass the exceptions through gtk
            vm->aikido->reportException (e) ;
            exit (1) ;
        }
    }
} ;

//std::list<SignalConnection*> signalConnections ;

#if 0
void aikido_signal_handler1 (GtkObject *obj, gpointer callback_data) {
    SignalConnection *conn = (SignalConnection*)callback_data ;
    conn->call() ;
}

void aikido_signal_handler0 (GtkObject *obj, gpointer callback_data) {
    SignalConnection *conn = (SignalConnection*)callback_data ;
    conn->call0() ;
}

#endif


int aikido_signal_marshal_BOOL__POINTER_INT_INT_UINT (void *object, void *p1, int p2, int p3, unsigned int p4, gpointer data) {
    SignalConnection *conn = (SignalConnection*)data ;
    return conn->call (p1, p2, p3) ;
}

int aikido_signal_marshal_BOOL__POINTER_STRING_STRING_POINTER (void *object, void *p1, char * p2, char * p3, void *p4, gpointer data) {
    SignalConnection *conn = (SignalConnection*)data ;
    return conn->call (p1, p2, p3, p4) ;
}

int aikido_signal_marshal_ENUM__ENUM (void *object, int p1, gpointer data) {
    SignalConnection *conn = (SignalConnection*)data ;
    return conn->call (p1) ;
}

int aikido_signal_marshal_NONE__BOOL (void *object, bool p1, gpointer data) {
    SignalConnection *conn = (SignalConnection*)data ;
    return conn->call (p1) ;
}

int aikido_signal_marshal_NONE__ENUM (void *object, int p1, gpointer data) {
    SignalConnection *conn = (SignalConnection*)data ;
    return conn->call (p1) ;
}

int aikido_signal_marshal_NONE__ENUM_FLOAT (void *object, int p1, float p2, gpointer data) {
    SignalConnection *conn = (SignalConnection*)data ;
    return conn->call (p1, (double)p2) ;
}

int aikido_signal_marshal_NONE__ENUM_FLOAT_BOOL (void *object, int p1, float p2, bool p3, gpointer data) {
    SignalConnection *conn = (SignalConnection*)data ;
    return conn->call (p1, (double)p2, p3) ;
}

int aikido_signal_marshal_NONE__INT (void *object, int p1, gpointer data) {
    SignalConnection *conn = (SignalConnection*)data ;
    return conn->call (p1) ;
}

int aikido_signal_marshal_NONE__INT_INT (void *object, int p1, int p2, gpointer data) {
    SignalConnection *conn = (SignalConnection*)data ;
    return conn->call (p1, p2) ;
}

int aikido_signal_marshal_NONE__INT_INT_POINTER (void *object, int p1, int p2, void *p3, gpointer data) {
    SignalConnection *conn = (SignalConnection*)data ;
    return conn->call (p1, p2, p3) ;
}

int aikido_signal_marshal_NONE__NONE (void *object, gpointer data) {
    SignalConnection *conn = (SignalConnection*)data ;
    return conn->call () ;
}

int aikido_signal_marshal_NONE__OBJECT (void *object, void *p1, gpointer data) {
    SignalConnection *conn = (SignalConnection*)data ;
    return conn->call (p1) ;
}

int aikido_signal_marshal_NONE__POINTER (void *object, void *p1, gpointer data) {
    SignalConnection *conn = (SignalConnection*)data ;
    return conn->call (p1) ;
}

int aikido_signal_marshal_NONE__POINTER_INT (void *object, void *p1, int p2, gpointer data) {
    SignalConnection *conn = (SignalConnection*)data ;
    return conn->call (p1, p2) ;
}

int aikido_signal_marshal_NONE__POINTER_INT_INT_POINTER_UINT_UINT (void *object, void *p1, int p2, int p3, void *p4, unsigned int p5, unsigned int p6, gpointer data) {
    SignalConnection *conn = (SignalConnection*)data ;
    return conn->call (p1, p2, p3, p4, p5, p6) ;
}

int aikido_signal_marshal_NONE__POINTER_INT_POINTER (void *object, void *p1, int p2, void *p3, gpointer data) {
    SignalConnection *conn = (SignalConnection*)data ;
    return conn->call (p1, p2, p3) ;
}

int aikido_signal_marshal_NONE__POINTER_POINTER (void *object, void *p1, void *p2, gpointer data) {
    SignalConnection *conn = (SignalConnection*)data ;
    return conn->call (p1, p2) ;
}

int aikido_signal_marshal_NONE__POINTER_POINTER_POINTER (void *object, void *p1, void *p2, void *p3, gpointer data) {
    SignalConnection *conn = (SignalConnection*)data ;
    return conn->call (p1, p2, p3) ;
}

int aikido_signal_marshal_NONE__POINTER_POINTER_UINT_UINT (void *object, void *p1, void *p2, unsigned int p3, unsigned int p4, gpointer data) {
    SignalConnection *conn = (SignalConnection*)data ;
    return conn->call (p1, p2, p3, p4) ;
}

int aikido_signal_marshal_NONE__POINTER_STRING_STRING (void *object, void *p1, char * p2, char * p3, gpointer data) {
    SignalConnection *conn = (SignalConnection*)data ;
    return conn->call (p1, p2, p3) ;
}

int aikido_signal_marshal_NONE__POINTER_UINT (void *object, void *p1, unsigned int p2, gpointer data) {
    SignalConnection *conn = (SignalConnection*)data ;
    return conn->call (p1, p2) ;
}

int aikido_signal_marshal_NONE__POINTER_UINT_UINT (void *object, void *p1, unsigned int p2, unsigned int p3, gpointer data) {
    SignalConnection *conn = (SignalConnection*)data ;
    return conn->call (p1, p2, p3) ;
}

int aikido_signal_marshal_NONE__STRING (void *object, char * p1, gpointer data) {
    SignalConnection *conn = (SignalConnection*)data ;
    return conn->call (p1) ;
}

int aikido_signal_marshal_NONE__UINT (void *object, unsigned int p1, gpointer data) {
    SignalConnection *conn = (SignalConnection*)data ;
    return conn->call (p1) ;
}

int aikido_signal_marshal_NONE__UINT_POINTER_UINT_UINT_ENUM (void *object, unsigned int p1, void *p2, unsigned int p3, unsigned int p4, int p5, gpointer data) {
    SignalConnection *conn = (SignalConnection*)data ;
    return conn->call (p1, p2, p3, p4, p5) ;
}

int aikido_signal_marshal_NONE__UINT_STRING (void *object, unsigned int p1, char * p2, gpointer data) {
    SignalConnection *conn = (SignalConnection*)data ;
    return conn->call (p1, p2) ;
}


int aikido_event_handler (GtkObject *obj, GdkEvent *event, gpointer callback_data) {
    SignalConnection *conn = (SignalConnection*)callback_data ;
    return conn->callevent(event).integer ;
    
}


//
// marshalles for signal_connect_object
//
int aikido_object_signal_marshal_BOOL__POINTER_INT_INT_UINT (void *object, void *p1, int p2, int p3, unsigned int p4, gpointer data) {
    SignalConnection *conn = (SignalConnection*)object ;
    return conn->call (p1, p2, p3) ;
}

int aikido_object_signal_marshal_BOOL__POINTER_STRING_STRING_POINTER (void *object, void *p1, char * p2, char * p3, void *p4, gpointer data) {
    SignalConnection *conn = (SignalConnection*)object ;
    return conn->call (p1, p2, p3, p4) ;
}

int aikido_object_signal_marshal_ENUM__ENUM (void *object, int p1, gpointer data) {
    SignalConnection *conn = (SignalConnection*)object ;
    return conn->call (p1) ;
}

int aikido_object_signal_marshal_NONE__BOOL (void *object, bool p1, gpointer data) {
    SignalConnection *conn = (SignalConnection*)object ;
    return conn->call (p1) ;
}

int aikido_object_signal_marshal_NONE__ENUM (void *object, int p1, gpointer data) {
    SignalConnection *conn = (SignalConnection*)object ;
    return conn->call (p1) ;
}

int aikido_object_signal_marshal_NONE__ENUM_FLOAT (void *object, int p1, float p2, gpointer data) {
    SignalConnection *conn = (SignalConnection*)object ;
    return conn->call (p1, (double)p2) ;
}

int aikido_object_signal_marshal_NONE__ENUM_FLOAT_BOOL (void *object, int p1, float p2, bool p3, gpointer data) {
    SignalConnection *conn = (SignalConnection*)object ;
    return conn->call (p1, (double)p2, p3) ;
}

int aikido_object_signal_marshal_NONE__INT (void *object, int p1, gpointer data) {
    SignalConnection *conn = (SignalConnection*)object ;
    return conn->call (p1) ;
}

int aikido_object_signal_marshal_NONE__INT_INT (void *object, int p1, int p2, gpointer data) {
    SignalConnection *conn = (SignalConnection*)object ;
    return conn->call (p1, p2) ;
}

int aikido_object_signal_marshal_NONE__INT_INT_POINTER (void *object, int p1, int p2, void *p3, gpointer data) {
    SignalConnection *conn = (SignalConnection*)object ;
    return conn->call (p1, p2, p3) ;
}

int aikido_object_signal_marshal_NONE__NONE (void *object, gpointer data) {
    SignalConnection *conn = (SignalConnection*)object ;
    return conn->call () ;
}

int aikido_object_signal_marshal_NONE__OBJECT (void *object, void *p1, gpointer data) {
    SignalConnection *conn = (SignalConnection*)object ;
    return conn->call (p1) ;
}

int aikido_object_signal_marshal_NONE__POINTER (void *object, void *p1, gpointer data) {
    SignalConnection *conn = (SignalConnection*)object ;
    return conn->call (p1) ;
}

int aikido_object_signal_marshal_NONE__POINTER_INT (void *object, void *p1, int p2, gpointer data) {
    SignalConnection *conn = (SignalConnection*)object ;
    return conn->call (p1, p2) ;
}

int aikido_object_signal_marshal_NONE__POINTER_INT_INT_POINTER_UINT_UINT (void *object, void *p1, int p2, int p3, void *p4, unsigned int p5, unsigned int p6, gpointer data) {
    SignalConnection *conn = (SignalConnection*)object ;
    return conn->call (p1, p2, p3, p4, p5, p6) ;
}

int aikido_object_signal_marshal_NONE__POINTER_INT_POINTER (void *object, void *p1, int p2, void *p3, gpointer data) {
    SignalConnection *conn = (SignalConnection*)object ;
    return conn->call (p1, p2, p3) ;
}

int aikido_object_signal_marshal_NONE__POINTER_POINTER (void *object, void *p1, void *p2, gpointer data) {
    SignalConnection *conn = (SignalConnection*)object ;
    return conn->call (p1, p2) ;
}

int aikido_object_signal_marshal_NONE__POINTER_POINTER_POINTER (void *object, void *p1, void *p2, void *p3, gpointer data) {
    SignalConnection *conn = (SignalConnection*)object ;
    return conn->call (p1, p2, p3) ;
}

int aikido_object_signal_marshal_NONE__POINTER_POINTER_UINT_UINT (void *object, void *p1, void *p2, unsigned int p3, unsigned int p4, gpointer data) {
    SignalConnection *conn = (SignalConnection*)object ;
    return conn->call (p1, p2, p3, p4) ;
}

int aikido_object_signal_marshal_NONE__POINTER_STRING_STRING (void *object, void *p1, char * p2, char * p3, gpointer data) {
    SignalConnection *conn = (SignalConnection*)object ;
    return conn->call (p1, p2, p3) ;
}

int aikido_object_signal_marshal_NONE__POINTER_UINT (void *object, void *p1, unsigned int p2, gpointer data) {
    SignalConnection *conn = (SignalConnection*)object ;
    return conn->call (p1, p2) ;
}

int aikido_object_signal_marshal_NONE__POINTER_UINT_UINT (void *object, void *p1, unsigned int p2, unsigned int p3, gpointer data) {
    SignalConnection *conn = (SignalConnection*)object ;
    return conn->call (p1, p2, p3) ;
}

int aikido_object_signal_marshal_NONE__STRING (void *object, char * p1, gpointer data) {
    SignalConnection *conn = (SignalConnection*)object ;
    return conn->call (p1) ;
}

int aikido_object_signal_marshal_NONE__UINT (void *object, unsigned int p1, gpointer data) {
    SignalConnection *conn = (SignalConnection*)object ;
    return conn->call (p1) ;
}

int aikido_object_signal_marshal_NONE__UINT_POINTER_UINT_UINT_ENUM (void *object, unsigned int p1, void *p2, unsigned int p3, unsigned int p4, int p5, gpointer data) {
    SignalConnection *conn = (SignalConnection*)object ;
    return conn->call (p1, p2, p3, p4, p5) ;
}

int aikido_object_signal_marshal_NONE__UINT_STRING (void *object, unsigned int p1, char * p2, gpointer data) {
    SignalConnection *conn = (SignalConnection*)object ;
    return conn->call (p1, p2) ;
}


int aikido_object_event_handler (GtkObject *object, GdkEvent *event, gpointer callback_data) {
    SignalConnection *conn = (SignalConnection*)object ;
    return conn->callevent(event).integer ;
    
}

GtkSignalFunc getMarshaller (int n) {
    switch (n) {
    case 0:
        return (GtkSignalFunc)aikido_event_handler ;
        break ;
    case 23:
        return (GtkSignalFunc)aikido_signal_marshal_BOOL__POINTER_INT_INT_UINT ;
        break ;
    case 20:
        return (GtkSignalFunc)aikido_signal_marshal_BOOL__POINTER_STRING_STRING_POINTER ;
        break ;
    case 11:
        return (GtkSignalFunc)aikido_signal_marshal_ENUM__ENUM ;
        break ;
    case 16:
        return (GtkSignalFunc)aikido_signal_marshal_NONE__BOOL ;
        break ;
    case 14:
        return (GtkSignalFunc)aikido_signal_marshal_NONE__ENUM ;
        break ;
    case 9:
        return (GtkSignalFunc)aikido_signal_marshal_NONE__ENUM_FLOAT ;
        break ;
    case 8:
        return (GtkSignalFunc)aikido_signal_marshal_NONE__ENUM_FLOAT_BOOL ;
        break ;
    case 7:
        return (GtkSignalFunc)aikido_signal_marshal_NONE__INT ;
        break ;
    case 6:
        return (GtkSignalFunc)aikido_signal_marshal_NONE__INT_INT ;
        break ;
    case 5:
        return (GtkSignalFunc)aikido_signal_marshal_NONE__INT_INT_POINTER ;
        break ;
    case 3:
        return (GtkSignalFunc)aikido_signal_marshal_NONE__NONE ;
        break ;
    case 22:
        return (GtkSignalFunc)aikido_signal_marshal_NONE__OBJECT ;
        break ;
    case 10:
        return (GtkSignalFunc)aikido_signal_marshal_NONE__POINTER ;
        break ;
    case 12:
        return (GtkSignalFunc)aikido_signal_marshal_NONE__POINTER_INT ;
        break ;
    case 25:
        return (GtkSignalFunc)aikido_signal_marshal_NONE__POINTER_INT_INT_POINTER_UINT_UINT ;
        break ;
    case 15:
        return (GtkSignalFunc)aikido_signal_marshal_NONE__POINTER_INT_POINTER ;
        break ;
    case 4:
        return (GtkSignalFunc)aikido_signal_marshal_NONE__POINTER_POINTER ;
        break ;
    case 13:
        return (GtkSignalFunc)aikido_signal_marshal_NONE__POINTER_POINTER_POINTER ;
        break ;
    case 24:
        return (GtkSignalFunc)aikido_signal_marshal_NONE__POINTER_POINTER_UINT_UINT ;
        break ;
    case 19:
        return (GtkSignalFunc)aikido_signal_marshal_NONE__POINTER_STRING_STRING ;
        break ;
    case 17:
        return (GtkSignalFunc)aikido_signal_marshal_NONE__POINTER_UINT ;
        break ;
    case 2:
        return (GtkSignalFunc)aikido_signal_marshal_NONE__POINTER_UINT_UINT ;
        break ;
    case 26:
        return (GtkSignalFunc)aikido_signal_marshal_NONE__STRING ;
        break ;
    case 21:
        return (GtkSignalFunc)aikido_signal_marshal_NONE__UINT ;
        break ;
    case 1:
        return (GtkSignalFunc)aikido_signal_marshal_NONE__UINT_POINTER_UINT_UINT_ENUM ;
        break ;
    case 18:
        return (GtkSignalFunc)aikido_signal_marshal_NONE__UINT_STRING ;
        break ;
    }
}


GtkSignalFunc getObjectMarshaller (int n) {
    switch (n) {
    case 0:
        return (GtkSignalFunc)aikido_event_handler ;
        break ;
    case 23:
        return (GtkSignalFunc)aikido_object_signal_marshal_BOOL__POINTER_INT_INT_UINT ;
        break ;
    case 20:
        return (GtkSignalFunc)aikido_object_signal_marshal_BOOL__POINTER_STRING_STRING_POINTER ;
        break ;
    case 11:
        return (GtkSignalFunc)aikido_object_signal_marshal_ENUM__ENUM ;
        break ;
    case 16:
        return (GtkSignalFunc)aikido_object_signal_marshal_NONE__BOOL ;
        break ;
    case 14:
        return (GtkSignalFunc)aikido_object_signal_marshal_NONE__ENUM ;
        break ;
    case 9:
        return (GtkSignalFunc)aikido_object_signal_marshal_NONE__ENUM_FLOAT ;
        break ;
    case 8:
        return (GtkSignalFunc)aikido_object_signal_marshal_NONE__ENUM_FLOAT_BOOL ;
        break ;
    case 7:
        return (GtkSignalFunc)aikido_object_signal_marshal_NONE__INT ;
        break ;
    case 6:
        return (GtkSignalFunc)aikido_object_signal_marshal_NONE__INT_INT ;
        break ;
    case 5:
        return (GtkSignalFunc)aikido_object_signal_marshal_NONE__INT_INT_POINTER ;
        break ;
    case 3:
        return (GtkSignalFunc)aikido_object_signal_marshal_NONE__NONE ;
        break ;
    case 22:
        return (GtkSignalFunc)aikido_object_signal_marshal_NONE__OBJECT ;
        break ;
    case 10:
        return (GtkSignalFunc)aikido_object_signal_marshal_NONE__POINTER ;
        break ;
    case 12:
        return (GtkSignalFunc)aikido_object_signal_marshal_NONE__POINTER_INT ;
        break ;
    case 25:
        return (GtkSignalFunc)aikido_object_signal_marshal_NONE__POINTER_INT_INT_POINTER_UINT_UINT ;
        break ;
    case 15:
        return (GtkSignalFunc)aikido_object_signal_marshal_NONE__POINTER_INT_POINTER ;
        break ;
    case 4:
        return (GtkSignalFunc)aikido_object_signal_marshal_NONE__POINTER_POINTER ;
        break ;
    case 13:
        return (GtkSignalFunc)aikido_object_signal_marshal_NONE__POINTER_POINTER_POINTER ;
        break ;
    case 24:
        return (GtkSignalFunc)aikido_object_signal_marshal_NONE__POINTER_POINTER_UINT_UINT ;
        break ;
    case 19:
        return (GtkSignalFunc)aikido_object_signal_marshal_NONE__POINTER_STRING_STRING ;
        break ;
    case 17:
        return (GtkSignalFunc)aikido_object_signal_marshal_NONE__POINTER_UINT ;
        break ;
    case 2:
        return (GtkSignalFunc)aikido_object_signal_marshal_NONE__POINTER_UINT_UINT ;
        break ;
    case 26:
        return (GtkSignalFunc)aikido_object_signal_marshal_NONE__STRING ;
        break ;
    case 21:
        return (GtkSignalFunc)aikido_object_signal_marshal_NONE__UINT ;
        break ;
    case 1:
        return (GtkSignalFunc)aikido_object_signal_marshal_NONE__UINT_POINTER_UINT_UINT_ENUM ;
        break ;
    case 18:
        return (GtkSignalFunc)aikido_object_signal_marshal_NONE__UINT_STRING ;
        break ;
    }
}


typedef std::map<std::string, int> SignalMap ;
SignalMap *signalmap ;


void initsignals() {

    signalmap = new SignalMap() ;

    (*signalmap)["add-accelerator"] = 1 ;
    (*signalmap)["remove-accelerator"] = 2 ;
    (*signalmap)["changed"] = 3 ;
    (*signalmap)["value_changed"] = 3 ;
    (*signalmap)["pressed"] = 3 ;
    (*signalmap)["released"] = 3 ;
    (*signalmap)["clicked"] = 3 ;
    (*signalmap)["enter"] = 3 ;
    (*signalmap)["leave"] = 3 ;
    (*signalmap)["month_changed"] = 3 ;
    (*signalmap)["day_selected"] = 3 ;
    (*signalmap)["day_selected_double_click"] = 3 ;
    (*signalmap)["prev_month"] = 3 ;
    (*signalmap)["next_month"] = 3 ;
    (*signalmap)["prev_year"] = 3 ;
    (*signalmap)["next_year"] = 3 ;
    (*signalmap)["toggled"] = 3 ;
    (*signalmap)["set_scroll_adjustments"] = 4 ;
    (*signalmap)["select_row"] = 5 ;
    (*signalmap)["unselect_row"] = 5 ;
    (*signalmap)["row_move"] = 6 ;
    (*signalmap)["click_column"] = 7 ;
    (*signalmap)["resize_column"] = 6 ;
    (*signalmap)["toggle_focus_row"] = 3 ;
    (*signalmap)["select_all"] = 3 ;
    (*signalmap)["unselect_all"] = 3 ;
    (*signalmap)["undo_selection"] = 3 ;
    (*signalmap)["start_selection"] = 3 ;
    (*signalmap)["end_selection"] = 3 ;
    (*signalmap)["toggle_add_mode"] = 3 ;
    (*signalmap)["extend_selection"] = 8 ;
    (*signalmap)["scroll_vertical"] = 9 ;
    (*signalmap)["scroll_horizontal"] = 9 ;
    (*signalmap)["abort_column_resize"] = 3 ;
    (*signalmap)["color_changed"] = 3 ;
    (*signalmap)["add"] = 10 ;
    (*signalmap)["remove"] = 10 ;
    (*signalmap)["check_resize"] = 3 ;
    (*signalmap)["focus"] = 11 ;
    (*signalmap)["set-focus-child"] = 10 ;
    (*signalmap)["tree_select_row"] = 12 ;
    (*signalmap)["tree_unselect_row"] = 12 ;
    (*signalmap)["tree_expand"] = 10 ;
    (*signalmap)["tree_collapse"] = 10 ;
    (*signalmap)["tree_move"] = 13 ;
    (*signalmap)["change_focus_row_expansion"] = 14 ;
    (*signalmap)["curve_type_changed"] = 3 ;
    (*signalmap)["disconnect"] = 3 ;
    (*signalmap)["changed"] = 3 ;
    (*signalmap)["insert_text"] = 15 ;
    (*signalmap)["delete_text"] = 6 ;
    (*signalmap)["activate"] = 3 ;
    (*signalmap)["set-editable"] = 16 ;
    (*signalmap)["move_cursor"] = 6 ;
    (*signalmap)["move_word"] = 7 ;
    (*signalmap)["move_page"] = 6 ;
    (*signalmap)["move_to_row"] = 7 ;
    (*signalmap)["move_to_column"] = 7 ;
    (*signalmap)["kill_char"] = 7 ;
    (*signalmap)["kill_word"] = 7 ;
    (*signalmap)["kill_line"] = 7 ;
    (*signalmap)["cut_clipboard"] = 3 ;
    (*signalmap)["copy_clipboard"] = 3 ;
    (*signalmap)["paste_clipboard"] = 3 ;
    (*signalmap)["child_attached"] = 10 ;
    (*signalmap)["child_detached"] = 10 ;
    (*signalmap)["enable_device"] = 7 ;
    (*signalmap)["disable_device"] = 7 ;
    (*signalmap)["select"] = 3 ;
    (*signalmap)["deselect"] = 3 ;
    (*signalmap)["toggle"] = 3 ;
    (*signalmap)["set_scroll_adjustments"] = 4 ;
    (*signalmap)["selection_changed"] = 3 ;
    (*signalmap)["select_child"] = 10 ;
    (*signalmap)["unselect_child"] = 10 ;
    (*signalmap)["toggle_focus_row"] = 3 ;
    (*signalmap)["select_all"] = 3 ;
    (*signalmap)["unselect_all"] = 3 ;
    (*signalmap)["undo_selection"] = 3 ;
    (*signalmap)["start_selection"] = 3 ;
    (*signalmap)["end_selection"] = 3 ;
    (*signalmap)["toggle_add_mode"] = 3 ;
    (*signalmap)["extend_selection"] = 8 ;
    (*signalmap)["scroll_vertical"] = 9 ;
    (*signalmap)["scroll_horizontal"] = 9 ;
    (*signalmap)["activate"] = 3 ;
    (*signalmap)["activate_item"] = 3 ;
    (*signalmap)["deactivate"] = 3 ;
    (*signalmap)["selection-done"] = 3 ;
    (*signalmap)["move_current"] = 14 ;
    (*signalmap)["activate_current"] = 16 ;
    (*signalmap)["cancel"] = 3 ;
    (*signalmap)["switch_page"] = 17 ;
    (*signalmap)["destroy"] = 3 ;
    (*signalmap)["text_pushed"] = 18 ;
    (*signalmap)["text_popped"] = 18 ;
    (*signalmap)["set_scroll_adjustments"] = 4 ;
    (*signalmap)["start_query"] = 3 ;
    (*signalmap)["stop_query"] = 3 ;
    (*signalmap)["widget_entered"] = 19 ;
    (*signalmap)["widget_selected"] = 20 ;
    (*signalmap)["toggled"] = 3 ;
    (*signalmap)["orientation_changed"] = 7 ;
    (*signalmap)["style_changed"] = 7 ;
    (*signalmap)["selection_changed"] = 3 ;
    (*signalmap)["select_child"] = 10 ;
    (*signalmap)["unselect_child"] = 10 ;
    (*signalmap)["expand"] = 3 ;
    (*signalmap)["collapse"] = 3 ;
    (*signalmap)["set_scroll_adjustments"] = 4 ;
    (*signalmap)["show"] = 3 ;
    (*signalmap)["hide"] = 3 ;
    (*signalmap)["map"] = 3 ;
    (*signalmap)["unmap"] = 3 ;
    (*signalmap)["realize"] = 3 ;
    (*signalmap)["unrealize"] = 3 ;
    (*signalmap)["draw"] = 10 ;
    (*signalmap)["draw_focus"] = 3 ;
    (*signalmap)["draw_default"] = 3 ;
    (*signalmap)["size_request"] = 10 ;
    (*signalmap)["size_allocate"] = 10 ;
    (*signalmap)["state_changed"] = 21 ;
    (*signalmap)["parent_set"] = 22 ;
    (*signalmap)["style_set"] = 10 ;
    (*signalmap)["grab_focus"] = 3 ;
    (*signalmap)["event"] = 0 ;
    (*signalmap)["button_press_event"] = 0 ;
    (*signalmap)["button_release_event"] = 0 ;
    (*signalmap)["motion_notify_event"] = 0 ;
    (*signalmap)["delete_event"] = 0 ;
    (*signalmap)["destroy_event"] = 0 ;
    (*signalmap)["expose_event"] = 0 ;
    (*signalmap)["key_press_event"] = 0 ;
    (*signalmap)["key_release_event"] = 0 ;
    (*signalmap)["enter_notify_event"] = 0 ;
    (*signalmap)["leave_notify_event"] = 0 ;
    (*signalmap)["configure_event"] = 0 ;
    (*signalmap)["focus_in_event"] = 0 ;
    (*signalmap)["focus_out_event"] = 0 ;
    (*signalmap)["map_event"] = 0 ;
    (*signalmap)["unmap_event"] = 0 ;
    (*signalmap)["property_notify_event"] = 0 ;
    (*signalmap)["selection_clear_event"] = 0 ;
    (*signalmap)["selection_request_event"] = 0 ;
    (*signalmap)["selection_notify_event"] = 0 ;
    (*signalmap)["selection_received"] = 17 ;
    (*signalmap)["selection_get"] = 2 ;
    (*signalmap)["proximity_in_event"] = 0 ;
    (*signalmap)["proximity_out_event"] = 0 ;
    (*signalmap)["drag_leave"] = 17 ;
    (*signalmap)["drag_begin"] = 10 ;
    (*signalmap)["drag_end"] = 10 ;
    (*signalmap)["drag_data_delete"] = 10 ;
    (*signalmap)["drag_motion"] = 23 ;
    (*signalmap)["drag_drop"] = 23 ;
    (*signalmap)["drag_data_get"] = 24 ;
    (*signalmap)["drag_data_received"] = 25 ;
    (*signalmap)["visibility_notify_event"] = 0 ;
    (*signalmap)["client_event"] = 0 ;
    (*signalmap)["no_expose_event"] = 0 ;
    (*signalmap)["debug_msg"] = 26 ;
    (*signalmap)["set_focus"] = 10 ;
}

GtkSignalFunc getSignalFunc (const string &name) {
    SignalMap::iterator i = signalmap->find (name.str) ;
    if (i == signalmap->end()) {
        throw Exception ("Unknown signal") ;
    }
    return getMarshaller (i->second) ;
}

GtkSignalFunc getObjectSignalFunc (const string &name) {
    SignalMap::iterator i = signalmap->find (name.str) ;
    if (i == signalmap->end()) {
        throw Exception ("Unknown signal") ;
    }
    return getObjectMarshaller (i->second) ;
}

gpointer toNative (const Value &obj, const char *func, VirtualMachine *vm, StackFrame *stack) {
    gpointer nativeobj = NULL ;
    if (obj.type != T_OBJECT) {
        throw newParameterException (vm, stack, func, "bad parameter type") ;
    }
    if (obj.object != NULL) {
        Operand tmp ;
        Scope *scope ;
        Variable *var = obj.object->block->findVariable (string(".toNative"), scope, VAR_PUBLIC, stack->inst->source, NULL) ;
        if (var == NULL) {
            throw newParameterException (vm, stack, func,  "No toNative() function found") ;
        }
        Tag *tag = (Tag*)var ;
        vm->callFunction (&tmp, tag->block, obj.object) ;
        nativeobj = (gpointer)tmp.val.integer ;
    }
    return nativeobj ;
}


// connect to a aikido signal function
AIKIDO_NATIVE(gtk_signal_connect) {
    if (paras[3].type != T_STRING) {
        throw newParameterException (vm, stack, "gtk_signal_connect", "bad signal name type") ;
    }
    GtkObject *gtkobj = (GtkObject*)paras[2].integer ;
    if (paras[4].type != T_CLOSURE) {
        if (paras[4].type != T_INTEGER) {
            throw newParameterException (vm, stack, "gtk_signal_connect", "bad signal function type") ;
        }
        gpointer nativeobj = toNative (paras[5], "gtk_signal_connect", vm, stack) ;
        return gtk_signal_connect (gtkobj, paras[3].str->c_str(), (GtkSignalFunc)paras[4].integer, nativeobj) ;
    }

    SignalConnection *conn = new SignalConnection(paras[1], gtkobj, *paras[3].str, paras[4].closure, vm, stack, paras[5]) ;
    //signalConnections.push_back (conn) ;

    conn->handler = gtk_signal_connect (gtkobj, paras[3].str->c_str(), getSignalFunc (*paras[3].str), (gpointer)conn) ;
    return (INTEGER)conn ;
}

AIKIDO_NATIVE(gtk_signal_connect_after) {
    if (paras[3].type != T_STRING) {
        throw newParameterException (vm, stack, "gtk_signal_connect_after", "bad signal name type") ;
    }
    GtkObject *gtkobj = (GtkObject*)paras[2].integer ;
    if (paras[4].type != T_CLOSURE) {
        if (paras[4].type != T_INTEGER) {
            throw newParameterException (vm, stack, "gtk_signal_connect_after", "bad signal function type") ;
        }
        gpointer nativeobj = toNative (paras[5], "gtk_signal_connect_after", vm, stack) ;
        return gtk_signal_connect_after (gtkobj, paras[3].str->c_str(), (GtkSignalFunc)paras[4].integer, nativeobj) ;
    }
    SignalConnection *conn = new SignalConnection(paras[1], gtkobj, *paras[3].str, paras[4].closure, vm, stack, paras[5]) ;
    //signalConnections.push_back (conn) ;

    conn->handler = gtk_signal_connect_after (gtkobj, paras[3].str->c_str(), getSignalFunc (*paras[3].str), (gpointer)conn) ;
    return (INTEGER)conn ;
}

AIKIDO_NATIVE(gtk_signal_connect_object) {
    if (paras[3].type != T_STRING) {
        throw newParameterException (vm, stack, "gtk_signal_connect_object", "bad signal name type") ;
    }
    GtkObject *gtkobj = (GtkObject*)paras[2].integer ;
    if (paras[4].type != T_CLOSURE) {
        if (paras[4].type != T_INTEGER) {
            throw newParameterException (vm, stack, "gtk_signal_connect_object", "bad signal function type") ;
        }
        GtkObject *nativeobj = (GtkObject*)toNative (paras[5], "gtk_signal_connect_object", vm, stack) ;
        return gtk_signal_connect_object (gtkobj, paras[3].str->c_str(), (GtkSignalFunc)paras[4].integer, nativeobj) ;
    }

    SignalConnection *conn = new SignalConnection(paras[1], gtkobj, *paras[3].str, paras[4].closure, vm, stack, paras[5], true) ;
    //signalConnections.push_back (conn) ;

    conn->handler = gtk_signal_connect_object (gtkobj, paras[3].str->c_str(), getObjectSignalFunc (*paras[3].str), (GtkObject*)conn) ;
    return (INTEGER)conn ;
}

//
// native functions that take floating point arguments
//

AIKIDO_NATIVE (gtk_adjustment_new) {
    GtkObject *w = gtk_adjustment_new (paras[1].real, paras[2].real, 
				      paras[3].real, paras[4].real,
 				      paras[5].real, paras[6].real) ;
    return (INTEGER)w ;
}


AIKIDO_NATIVE (gtk_adjustment_clamp_page) {
    gtk_adjustment_clamp_page ((GtkAdjustment   *)paras[1].integer, paras[2].real, paras[3].real) ;
    return 0 ;
}

AIKIDO_NATIVE (gtk_adjustment_set_value) {
    gtk_adjustment_set_value ((GtkAdjustment   *)paras[1].integer, paras[2].real) ;
    return 0 ;
}

AIKIDO_NATIVE (gtk_alignment_new) {
    GtkWidget *w = gtk_alignment_new (paras[1].real, paras[2].real, 

				      paras[3].real, paras[4].real) ;
    return (INTEGER)w ;
}

AIKIDO_NATIVE (gtk_alignment_set) {
    gtk_alignment_set ((GtkAlignment*)paras[1].integer, paras[2].real, 
				      paras[3].real, paras[4].real, paras[5].real) ;
    return 0 ;
}

AIKIDO_NATIVE (gtk_aspect_frame_new) {
    GtkWidget *w = gtk_aspect_frame_new (paras[1].str->c_str(), paras[2].real, 
				      paras[3].real, paras[4].real, paras[5].integer) ;
    return (INTEGER)w ;
}

AIKIDO_NATIVE (gtk_aspect_frame_set) {
    gtk_aspect_frame_set ((GtkAspectFrame*)paras[1].integer, paras[2].real, 
				      paras[3].real, paras[4].real, paras[5].integer) ;
    return 0 ;
}

AIKIDO_NATIVE (gtk_clist_moveto) {
    gtk_clist_moveto ((GtkCList *)paras[1].integer, paras[2].integer, paras[3].integer,
				  paras[4].real, paras[5].real) ;
    return 0 ;
}

AIKIDO_NATIVE (gtk_ctree_node_moveto) {
    gtk_ctree_node_moveto ((GtkCTree *)paras[1].integer, (GtkCTreeNode*)paras[2].integer, paras[3].integer,
				  paras[4].real, paras[5].real) ;
    return 0 ;
}

// make a char[] from a vector of strings
const char **make_clist_row (int ncols, const Value &v) {
    const char **row = new const char *[ncols] ;
    for (int i = 0 ; i < ncols ; i++) {
        row[i] = (*v.vec)[i].str->c_str() ;
    }
    return row ;
}


AIKIDO_NATIVE (gtk_ctree_insert_node) {
    const char **text = make_clist_row (paras[4].vec->vec.size(), paras[4]) ;
    gtk_ctree_insert_node ((GtkCTree*)paras[1].integer, (GtkCTreeNode*)paras[2].integer, (GtkCTreeNode*)paras[3].integer, (char**)text,
                           paras[5].integer,
                           (GdkPixmap*)paras[6].integer,
                           (GdkBitmap*)paras[7].integer,
                           (GdkPixmap*)paras[8].integer,
                           (GdkBitmap*)paras[9].integer,
                           paras[10].integer,
                           paras[11].integer) ;
    delete [] text ;
    return 0 ;		// XXX: real return value?
}

AIKIDO_NATIVE (gtk_curve_set_gamma) {
    gtk_curve_set_gamma ((GtkCurve *)paras[1].integer, paras[2].real) ;
    return 0 ;
}


AIKIDO_NATIVE (gtk_curve_set_range) {
    gtk_curve_set_range ((GtkCurve *)paras[1].integer, paras[2].real,
    				     paras[3].real, paras[4].real, paras[5].real) ;
    return 0 ;
}

AIKIDO_NATIVE (gtk_curve_get_vector) {
    int veclen = paras[2].integer ;
    gfloat *fvec = (gfloat*)alloca (veclen * sizeof (gfloat)) ;
    gtk_curve_get_vector ((GtkCurve *)paras[1].integer, paras[2].integer, fvec) ;
    // XXX: need to copy vector to reference para 3
    return 0 ;
}

AIKIDO_NATIVE (gtk_curve_set_vector) {
    int veclen = paras[2].integer ;
    gfloat *fvec = (gfloat*)alloca (veclen * sizeof (gfloat)) ;
    Value::vector *vec = paras[3].vec ;
    for (int i = 0 ; i < veclen ; i++) {
        fvec[i] = (*vec)[i].real ;
    }
    gtk_curve_set_vector ((GtkCurve *)paras[1].integer, paras[2].integer, fvec) ;
    return 0 ;
}

AIKIDO_NATIVE (gtk_frame_set_label_align) {
    gtk_frame_set_label_align ((GtkFrame*)paras[1].integer, paras[2].real, paras[3].real) ;
    return 0 ;
}

AIKIDO_NATIVE (gtk_list_extend_selection) {
    gtk_list_extend_selection ((GtkList*)paras[1].integer, (GtkScrollType)getInt(paras[2]), paras[3].real,
					 paras[4].integer) ;
    return 0 ;
}

AIKIDO_NATIVE (gtk_list_scroll_horizontal) {
    gtk_list_scroll_horizontal ((GtkList*)paras[1].integer, (GtkScrollType)getInt(paras[2]), paras[3].real) ;
    return 0 ;
}

AIKIDO_NATIVE (gtk_list_scroll_vertical) {
    gtk_list_scroll_vertical ((GtkList*)paras[1].integer, (GtkScrollType)getInt(paras[2]), paras[3].real) ;
    return 0 ;
}

AIKIDO_NATIVE (gtk_misc_set_alignment) {
    gtk_misc_set_alignment ((GtkMisc*)paras[1].integer, paras[2].real, paras[3].real) ;
    return 0 ;
}


AIKIDO_NATIVE (gtk_progress_set_text_alignment) {
    gtk_progress_set_text_alignment ((GtkProgress*)paras[1].integer, paras[2].real, paras[3].real) ;
    return 0 ;
}


AIKIDO_NATIVE (gtk_progress_configure) {
    gtk_progress_configure ((GtkProgress*)paras[1].integer, paras[2].real, paras[3].real, paras[4].real) ;
    return 0 ;
}


AIKIDO_NATIVE (gtk_progress_set_percentage) {
    gtk_progress_set_percentage ((GtkProgress*)paras[1].integer, paras[2].real) ;
    return 0 ;
}

AIKIDO_NATIVE (gtk_progress_set_value) {
    gtk_progress_set_value ((GtkProgress*)paras[1].integer, paras[2].real) ;
    return 0 ;
}

AIKIDO_NATIVE (gtk_progress_get_value) {
    return (double)gtk_progress_get_value ((GtkProgress*)paras[1].integer) ;
}

AIKIDO_NATIVE (gtk_progress_get_text_from_value) {
    return new string (gtk_progress_get_text_from_value ((GtkProgress*)paras[1].integer, paras[2].real)) ;
}

AIKIDO_NATIVE (gtk_progress_get_current_percentage) {
    return (double)gtk_progress_get_current_percentage ((GtkProgress*)paras[1].integer) ;
}


AIKIDO_NATIVE (gtk_progress_get_percentage_from_value) {
    return (double)gtk_progress_get_percentage_from_value ((GtkProgress*)paras[1].integer, paras[2].real) ;
}

AIKIDO_NATIVE (gtk_range_trough_click) {
    gfloat jumpperc ;
    int r = gtk_range_trough_click ((GtkRange*)paras[1].integer, paras[2].integer, paras[3].integer, &jumpperc) ;
    *paras[4].addr = jumpperc ;
    return (INTEGER)r ;
}


AIKIDO_NATIVE (gtk_range_default_htrough_click) {
    gfloat jumpperc ;
    int r = gtk_range_default_htrough_click ((GtkRange*)paras[1].integer, paras[2].integer, paras[3].integer, &jumpperc) ;
    *paras[4].addr = jumpperc ;
    return (INTEGER)r ;
}


AIKIDO_NATIVE (gtk_range_default_vtrough_click) {
    gfloat jumpperc ;
    int r = gtk_range_default_vtrough_click ((GtkRange*)paras[1].integer, paras[2].integer, paras[3].integer, &jumpperc) ;
    *paras[4].addr = jumpperc ;
    return (INTEGER)r ;
}


AIKIDO_NATIVE (gtk_ruler_set_range) {
    gtk_ruler_set_range ((GtkRuler*)paras[1].integer, paras[2].real, paras[3].real, paras[4].real, paras[5].real) ;
    return 0 ;
}

AIKIDO_NATIVE (gtk_spin_button_configure) {
    gtk_spin_button_configure ((GtkSpinButton*)paras[1].integer, (GtkAdjustment*)paras[2].integer, 
 						paras[3].real, paras[4].integer) ;
    return 0 ;
}

AIKIDO_NATIVE (gtk_spin_button_new) {
    GtkWidget *w = gtk_spin_button_new ((GtkAdjustment*)paras[1].integer, paras[2].real, paras[3].integer) ;
    return (INTEGER)w ;
}

AIKIDO_NATIVE (gtk_spin_button_get_value_as_float) {
    return (double)gtk_spin_button_get_value_as_float ((GtkSpinButton*)paras[1].integer) ;
}

AIKIDO_NATIVE (gtk_spin_button_set_value) {
    gtk_spin_button_set_value ((GtkSpinButton*)paras[1].integer, paras[2].real) ;
    return 0 ;
}

AIKIDO_NATIVE (gtk_spin_button_spin) {
    gtk_spin_button_spin ((GtkSpinButton*)paras[1].integer, (GtkSpinType)getInt(paras[2]), paras[3].real) ;
    return 0 ;
}


AIKIDO_NATIVE (dtk_widget_get_window) {
    GtkWidget *w = (GtkWidget*)paras[1].integer ;
    return (INTEGER)w->window ;
}

AIKIDO_NATIVE (dtk_widget_get_allocation) {
    GtkWidget *w = (GtkWidget*)paras[1].integer ;
    return (INTEGER)&w->allocation ;
}

AIKIDO_NATIVE (dtk_widget_get_state) {
    GtkWidget *w = (GtkWidget*)paras[1].integer ;
    return (INTEGER)w->state ;
}


// these take a Rectangle object

AIKIDO_NATIVE (gtk_widget_draw) {
    GdkRectangle rect ;
    rect.x = paras[2].object->varstack[1].integer ;
    rect.y = paras[2].object->varstack[2].integer ;
    rect.width = paras[2].object->varstack[3].integer ;
    rect.height = paras[2].object->varstack[4].integer ;
    gtk_widget_draw ((GtkWidget*)paras[1].integer, &rect) ;
    return 0 ;
}


AIKIDO_NATIVE (gtk_widget_intersect) {
    GdkRectangle area ;
    GdkRectangle intersection ;
    area.x = paras[2].object->varstack[1].integer ;
    area.y = paras[2].object->varstack[2].integer ;
    area.width = paras[2].object->varstack[3].integer ;
    area.height = paras[2].object->varstack[4].integer ;

    intersection.x = paras[3].object->varstack[1].integer ;
    intersection.y = paras[3].object->varstack[2].integer ;
    intersection.width = paras[3].object->varstack[3].integer ;
    intersection.height = paras[3].object->varstack[4].integer ;
    gtk_widget_intersect ((GtkWidget*)paras[1].integer, &area, &intersection) ;
    return 0 ;
}

AIKIDO_NATIVE (dtk_text_get_adjustments) {
    GtkText *t = (GtkText*)paras[1].integer ;
    *paras[2].addr = (INTEGER)t->vadj ;
    *paras[3].addr = (INTEGER)t->hadj ;
    return 0 ;
}

AIKIDO_NATIVE (gtk_editable_get_chars) {
    char *p = gtk_editable_get_chars ((GtkEditable*)paras[1].integer, paras[2].integer, paras[3].integer) ;
    string *s = new string (p) ;
    return s ;
}

AIKIDO_NATIVE (dtk_text_get_char) {
    GtkText *t = (GtkText*)paras[1].integer ;
    return GTK_TEXT_INDEX (t, paras[2].integer) ;
}

AIKIDO_NATIVE (dtk_dialog_get_vbox) {
    GtkDialog *d = (GtkDialog*)paras[1].integer ;
    return (INTEGER)d->vbox ;
}

AIKIDO_NATIVE (dtk_dialog_get_action_area) {
    GtkDialog *d = (GtkDialog*)paras[1].integer ;
    return (INTEGER)d->action_area ;
}

AIKIDO_NATIVE (dtk_widget_set_flags) {
    GtkWidget *w = (GtkWidget*)paras[1].integer ;
    GTK_WIDGET_SET_FLAGS (w, getInt (paras[2])) ;
    return 0 ;
}


struct Timeout {
    Timeout (Block *f, VirtualMachine *v, StackFrame *s, StaticLink *sl, const Value &d) : func (f),
        vm(v), stack(mainStack), slink(sl), data(d) {}
    Block *func ;
    VirtualMachine *vm ;
    StackFrame *stack ;
    StaticLink *slink ;
    Value data ;
    guint id ;

    int call() {
        Value args[2] ;
        args[0] = data ;
        args[1] = data ;
        return vm->call (func, stack, slink, 2, args) ;
    }

} ;

int dtk_timeout_signal (gpointer data) {
    Timeout *t = (Timeout*)data ;
    return t->call() ;
}

AIKIDO_NATIVE (gtk_timeout_add) {
    int time = getInt (paras[1]) ;
    const Value &func = paras[2] ;
    const Value &data = paras[3] ;

    Timeout *t = new Timeout (func.closure->block, vm, stack, func.closure->slink, data) ;
    guint tm = gtk_timeout_add (time, dtk_timeout_signal, t) ;
    t->id = tm ;
    return (INTEGER)t ;
}

AIKIDO_NATIVE (gtk_timeout_remove) {
    Timeout *t = (Timeout*)paras[1].integer ;
    gtk_timeout_remove (t->id) ;
    delete t ;
    return 0 ;
}

AIKIDO_NATIVE (gtk_idle_add) {
    const Value &func = paras[1] ;
    const Value &data = paras[2] ;

    Timeout *t = new Timeout (func.closure->block, vm, stack, func.closure->slink, data) ;
    guint i = gtk_idle_add (dtk_timeout_signal, t) ;
    t->id = i ;
    return (INTEGER)i ;
}

AIKIDO_NATIVE (gtk_idle_remove) {
    Timeout *t = (Timeout*)paras[1].integer ;
    gtk_idle_remove (t->id) ;
    delete t ;
    return 0 ;
}


AIKIDO_NATIVE( dtk_adjustment_get_upper) {
    GtkAdjustment *a = (GtkAdjustment*)paras[1].integer ;
    return (double)a->upper ;
}

AIKIDO_NATIVE( dtk_adjustment_get_lower) {
    GtkAdjustment *a = (GtkAdjustment*)paras[1].integer ;
    return (double)a->lower ;
}

AIKIDO_NATIVE( dtk_adjustment_get_value) {
    GtkAdjustment *a = (GtkAdjustment*)paras[1].integer ;
    return (double)a->value ;
}

AIKIDO_NATIVE( dtk_adjustment_get_step_increment) {
    GtkAdjustment *a = (GtkAdjustment*)paras[1].integer ;
    return (double)a->step_increment ;
}

AIKIDO_NATIVE( dtk_adjustment_get_page_increment) {
    GtkAdjustment *a = (GtkAdjustment*)paras[1].integer ;
    return (double)a->page_increment ;
}

AIKIDO_NATIVE( dtk_adjustment_get_page_size) {
    GtkAdjustment *a = (GtkAdjustment*)paras[1].integer ;
    return (double)a->page_size ;
}

AIKIDO_NATIVE (dtk_progress_get_adjustment) {
    GtkProgress *p = (GtkProgress*)paras[1].integer ;
    return (INTEGER)p->adjustment ;
}

AIKIDO_NATIVE (gtk_file_selection_get_filename) {
    return new string (gtk_file_selection_get_filename ((GtkFileSelection*)paras[1].integer)) ;
}

AIKIDO_NATIVE (gtk_label_get) {
    char *buf ;
    gtk_label_get ((GtkLabel*)paras[1].integer, &buf) ;
    *paras[2].addr = new string (buf) ;
    return 0 ;
}

#if 0
// I get an undefined tempate symbol when I try to link this
// code.  Must be a bug in the compiler or linker.

typedef std::map<int, Object*> ObjectRegistry ;
ObjectRegistry *objectregistry ;

void initObjectRegistry() {
    objectregistry = new ObjectRegistry() ;
}

AIKIDO_NATIVE (dtk_register_object) {
    (*objectregistry)[paras[2].integer] = paras[1].object ;
    return NULL ;
}

AIKIDO_NATIVE (dtk_lookup_object) {
    ObjectRegistry::iterator i = objectregistry->find (paras[1].integer) ;
    if (i == objectregistry->end()) {
        return NULL ;
    }
    return i->second ;
}

AIKIDO_NATIVE (dtk_unregister_object) {
    ObjectRegistry::iterator i = objectregistry->find (paras[1].integer) ;
    if (i != objectregistry->end()) {
        objectregistry->erase (i) ;
    }
    return NULL ;
}
#endif

//
// Clist functions
//

AIKIDO_NATIVE (gtk_clist_append) {
    GtkCList *list = (GtkCList *)paras[1].integer ;
    const char **row = make_clist_row (list->columns, paras[2]) ;
    int v = gtk_clist_append (list, (char**)row) ;
    delete [] row ;
    return (INTEGER)v ;
}

AIKIDO_NATIVE (gtk_clist_prepend) {
    GtkCList *list = (GtkCList *)paras[1].integer ;
    const char **row = make_clist_row (list->columns, paras[2]) ;
    int v = gtk_clist_prepend (list, (char**)row) ;
    delete [] row ;
    return (INTEGER)v ;
}

AIKIDO_NATIVE (gtk_clist_insert) {
    GtkCList *list = (GtkCList *)paras[1].integer ;
    const char **row = make_clist_row (list->columns, paras[3]) ;
    int v = gtk_clist_insert (list, paras[2].integer, (char**)row) ;
    delete [] row ;
    return (INTEGER)v ;
}

AIKIDO_NATIVE (gtk_clist_set_row_data) {
    GtkCList *list = (GtkCList *)paras[1].integer ;
    gtk_clist_set_row_data (list, paras[2].integer, (gpointer)paras[3].integer) ;
    return 0 ;
}

AIKIDO_NATIVE (gtk_clist_find_row_from_data) {
    GtkCList *list = (GtkCList *)paras[1].integer ;
    return gtk_clist_find_row_from_data (list, (gpointer)paras[2].integer) ;
}


AIKIDO_NATIVE (gtk_clist_get_selection_info) {
    GtkCList *list = (GtkCList *)paras[1].integer ;
    int r, c ;
    INTEGER v =  gtk_clist_get_selection_info (list, paras[2].integer, paras[3].integer, &r, &c) ;
    *paras[4].addr = r ;
    *paras[5].addr = c ;
    return v ;
}

AIKIDO_NATIVE (gtk_clist_new_with_titles) {
    const char **titles = make_clist_row (paras[1].integer, paras[2]) ;
    GtkWidget *list = gtk_clist_new_with_titles (paras[1].integer, (char**)titles) ;
    delete [] titles ;
    return (INTEGER)list ;
}

AIKIDO_NATIVE (gtk_ctree_new_with_titles) {
    const char **titles = make_clist_row (paras[1].integer, paras[3]) ;
    GtkWidget *tree = gtk_ctree_new_with_titles (paras[1].integer, paras[2].integer, (char**)titles) ;
    delete [] titles ;
    return (INTEGER)tree ;
}

AIKIDO_NATIVE (gtk_clist_get_column_title) {
    GtkCList *list = (GtkCList *)paras[1].integer ;
    char *title = gtk_clist_get_column_title (list, paras[2].integer) ;
    return new string (title) ;
}

AIKIDO_NATIVE (gtk_clist_get_text) {
    GtkCList *list = (GtkCList *)paras[1].integer ;
    char *text ;
    int v = gtk_clist_get_text (list, paras[2].integer, paras[3].integer, &text) ;
    *paras[4].addr = text ;
    return v ;
}

AIKIDO_NATIVE (gtk_clist_get_pixmap) {
    GtkCList *list = (GtkCList *)paras[1].integer ;
    GdkPixmap *pm ;
    GdkBitmap *bm ;
    int v = gtk_clist_get_pixmap (list, paras[2].integer, paras[3].integer, &pm, &bm) ;
    *paras[4].addr = (INTEGER)pm ;
    *paras[5].addr = (INTEGER)bm ;
    return v ;
}

AIKIDO_NATIVE (gtk_clist_get_pixtext) {
    GtkCList *list = (GtkCList *)paras[1].integer ;
    GdkPixmap *pm ;
    GdkBitmap *bm ;
    char *text ;
    guint8 spacing ;
    int v = gtk_clist_get_pixtext (list, paras[2].integer, paras[3].integer, &text, &spacing, &pm, &bm) ;
    *paras[4].addr = text ;
    *paras[5].addr = (INTEGER)spacing ;
    *paras[6].addr = (INTEGER)pm ;
    *paras[7].addr = (INTEGER)bm ;
    return v ;
}


AIKIDO_NATIVE (gtk_entry_get_text) {
    GtkEntry *entry = (GtkEntry*)paras[1].integer ;
    return new string (gtk_entry_get_text (entry)) ;
}

AIKIDO_NATIVE (gtk_drag_source_set) {
    GtkWidget *widget = (GtkWidget*)paras[1].integer ;
    GdkModifierType  start_button_mask = (GdkModifierType)paras[2].integer ;
    // we now have a vector of vectors.
    int n_targets = paras[4].integer ;
    GtkTargetEntry *targets = new GtkTargetEntry [n_targets] ;
    GdkDragAction actions = (GdkDragAction)paras[5].integer ;

    Value::vector *targetvec = paras[3].vec ;
    for (int i = 0 ; i < targetvec->size() ; i++) {
        Value::vector *t = targetvec->vec[i].vec ;
        targets[i].target = (gchar*)t->vec[0].str->c_str() ;
        targets[i].flags = t->vec[1].integer ;
        targets[i].info = t->vec[2].integer ;
    }
    gtk_drag_source_set (widget, start_button_mask, targets, n_targets, actions) ;
    return 0 ;
}

AIKIDO_NATIVE (gtk_drag_dest_set) {
    GtkWidget *widget = (GtkWidget*)paras[1].integer ;
    GtkDestDefaults  flags = (GtkDestDefaults)paras[2].integer ;
    // we now have a vector of vectors.
    int n_targets = paras[4].integer ;
    GtkTargetEntry *targets = new GtkTargetEntry [n_targets] ;
    GdkDragAction actions = (GdkDragAction)paras[5].integer ;

    Value::vector *targetvec = paras[3].vec ;
    for (int i = 0 ; i < targetvec->size() ; i++) {
        Value::vector *t = targetvec->vec[i].vec ;
        targets[i].target = (gchar*)t->vec[0].str->c_str() ;
        targets[i].flags = t->vec[1].integer ;
        targets[i].info = t->vec[2].integer ;
    }
    gtk_drag_dest_set (widget, flags, targets, n_targets, actions) ;
    return 0 ;
}

#include "dtkfields.cc"		// generated file
#include "dtkevents.cc"		// generated file

}


}

