/* Copyright (C) 2010 Ian MacLarty */
#ifndef LTCOMMON_H
#define LTCOMMON_H

#ifdef LINUX
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#else
#include <OpenGL/GL.h>
#endif

#include <stdlib.h>
#include <stdio.h>

#define ASSERT_CONCAT_(a, b) a##b
#define ASSERT_CONCAT(a, b) ASSERT_CONCAT_(a, b)
#define ct_assert(e) enum { ASSERT_CONCAT(assert_line_, __LINE__) = 1/(!!(e)) }

#define LT_PI 3.14159265358979323846f
#define LT_RADIANS_PER_DEGREE (LT_PI / 180.0f)
#define LT_DEGREES_PER_RADIAN (180.0f / LT_PI)

typedef float           LTfloat;
typedef unsigned int    LTuint;
typedef float           LTsecs;
typedef float           LTdegrees;
typedef unsigned int    LTuint32;
ct_assert(sizeof(LTuint32) == 4);

typedef LTuint32        LTpixel;
typedef GLuint          LTvertbuf;
typedef GLuint          LTtexbuf;

void ltAbort(const char *fmt, ...);
void ltLog(const char *fmt, ...);

// Used for reflection.  Any subclasses of LTObject that can be used
// in Lua code should have an entry in this enumeration.
enum LTType {
    LT_TYPE_OBJECT,
    LT_TYPE_SCENENODE,
    LT_TYPE_TRANSLATE,
    LT_TYPE_ROTATE,
    LT_TYPE_SCALE,
    LT_TYPE_TINT,
    LT_TYPE_LAYER,
    LT_TYPE_IMAGE,
    LT_TYPE_ATLAS,
    LT_TYPE_WORLD,
    LT_TYPE_BODY,
    LT_TYPE_FIXTURE,
    LT_TYPE_LINE,
    LT_TYPE_TRIANGLE,
    LT_TYPE_RECT,
    LT_NUM_TYPES
};

const char* ltTypeName(LTType type);

struct LTObject {
    LTType type;
    int ref_count;

    // A pointer to lua allocated userdata containing this object or
    // NULL if lua has no references to it.
    // This is used to return the same user data to lua for the same
    // LTObject, which avoids an extra allocation and problems with
    // equality and hashing being different for the same LTObject reference.
    LTObject **lua_userdata; 

    // The user can add any extra fields they want to an LTObject from
    // Lua code.  This field holds a reference to a table used to store
    // those fields.
    int lua_extra_fields_ref;

    LTObject(LTType type);
    virtual ~LTObject();

    virtual void retain();
    virtual void release();

    // For tweening.
    virtual LTfloat* field_ptr(const char *field_name);

    // Is this object of a certain type?
    bool hasType(LTType t);

    const char* typeName();
};

inline void ltRetain(LTObject *o) {
    o->ref_count++;
    //ltLog("RC+ %p (%s): %d", o, ltTypeName(o->type), o->ref_count);
}

inline void ltRelease(LTObject *o) {
    o->ref_count--;
    //ltLog("RC- %p (%s): %d", o, ltTypeName(o->type), o->ref_count);
    if (o->ref_count <= 0) {
        delete o;
    }
}

#endif
