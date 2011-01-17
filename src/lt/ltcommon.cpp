/* Copyright (C) 2010 Ian MacLarty */
#include "ltcommon.h"

#include <cstdarg>

//-----------------------------------------------------------------------
// Logging.

void ltAbort(const char *fmt, ...) {
    va_list argp;
    fprintf(stderr, "Error: ");
    va_start(argp, fmt);
    vfprintf(stderr, fmt, argp);
    va_end(argp);
    fprintf(stderr, "\n");
    exit(1);
}

void ltLog(const char *fmt, ...) {
    va_list argp;
    va_start(argp, fmt);
    vfprintf(stderr, fmt, argp);
    va_end(argp);
    fprintf(stderr, "\n");
}

//-----------------------------------------------------------------------
// LTObject.

struct LTTypeInfo {
    const char *name;
    LTType super_type;
};

const LTTypeInfo types[] = {
    {"Object",      LT_TYPE_OBJECT},
    {"Prop",        LT_TYPE_OBJECT},
    {"Translator",  LT_TYPE_PROP},  
    {"Rotator",     LT_TYPE_PROP},  
    {"Scalor",      LT_TYPE_PROP},  
    {"Tinter",      LT_TYPE_PROP},  
    {"Scene",       LT_TYPE_PROP},  
    {"Image",       LT_TYPE_PROP},  
    {"Atlas",       LT_TYPE_OBJECT},
    {"World",       LT_TYPE_OBJECT},
    {"Body",        LT_TYPE_PROP},
    {"Fixture",     LT_TYPE_PROP},
    {"Line",        LT_TYPE_PROP},
    {"Triangle",    LT_TYPE_PROP},
};

ct_assert(sizeof(types) == (int)LT_NUM_TYPES * sizeof(LTTypeInfo));

const char* ltTypeName(LTType type) {
    return types[type].name;
}

LTObject::LTObject(LTType type) {
    LTObject::type = type;
    ref_count = 0;
    lua_userdata = NULL;
}

LTObject::~LTObject() {
}

void LTObject::retain() {
    ltRetain(this);
}

void LTObject::release() {
    ltRelease(this);
}

LTfloat* LTObject::field_ptr(const char *field_name) {
    return NULL;
}

bool LTObject::hasType(LTType t) {
    if (t == LT_TYPE_OBJECT) {
        return true;
    }
    LTType t1 = type;
    while (t1 != LT_TYPE_OBJECT) {
        if (t == t1) {
            return true;
        }
        t1 = types[t1].super_type;
    }
    return false;
}

const char* LTObject::typeName() {
    return ltTypeName(type);
}
