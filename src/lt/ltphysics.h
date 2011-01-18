/* Copyright (C) 2010 Ian MacLarty */
#ifndef LTPHYSICS_H
#define LTPHYSICS_H

#include "Box2D/Box2D.h"

#include "ltcommon.h"
#include "ltgraphics.h"

struct LTWorld : LTObject {
    b2World *world;

    LTWorld(b2Vec2 gravity, bool doSleep);
    virtual ~LTWorld();
};

struct LTBody : LTProp {
    b2Body *body; // May be null if the body is destroyed.
    LTWorld *world;

    // Userdata is always a pointer to the LTBody object, so
    // leave it null in the body def.
    LTBody(LTWorld *world, const b2BodyDef *def);

    virtual void retain();
    virtual void release();

    void destroy();

    virtual void draw();
};

struct LTFixture : LTProp {
    b2Fixture *fixture; // May be null if the fixture is destroyed.
    LTBody *body;

    // Userdata is always a pointer to the LTFixture object, so
    // leave it null in the fixture def. 
    LTFixture(LTBody *body, const b2FixtureDef *def);

    virtual void retain();
    virtual void release();

    void destroy();

    virtual void draw();
};

// Check if Box2D will allow the polygon to be attached to a body
// without an assertion failure.
bool ltCheckB2Poly(const b2Vec2* vs, int32 count);

#endif