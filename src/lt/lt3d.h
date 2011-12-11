/* Copyright (C) 2011 Ian MacLarty */
#ifndef LT3D_H
#define LT3D_H

#include "ltcommon.h"
#include "ltscene.h"

struct LTPerspective : LTWrapNode {
    LTfloat near;
    LTfloat origin;
    LTfloat far;
    bool depth_buffer_on;

    LTPerspective(LTfloat near, LTfloat origin, LTfloat far, bool depth_buf_on, LTSceneNode *child);
    
    virtual void draw();
    bool propogatePointerEvent(LTfloat x, LTfloat y, LTPointerEvent *event);
    virtual LTfloat* field_ptr(const char *field_name);
};

struct LTPitch : LTWrapNode {
    LTfloat pitch;

    LTPitch(LTfloat pitch, LTSceneNode *child);

    virtual void draw();
    bool propogatePointerEvent(LTfloat x, LTfloat y, LTPointerEvent *event);
    virtual LTfloat* field_ptr(const char *field_name);
};

struct LTFog : LTWrapNode {
    LTfloat start;
    LTfloat end;
    LTColor color;

    LTFog(LTfloat start, LTfloat end, LTColor color, LTSceneNode *child);

    virtual void draw();
    bool propogatePointerEvent(LTfloat x, LTfloat y, LTPointerEvent *event);
    virtual LTfloat* field_ptr(const char *field_name);
};

#endif
