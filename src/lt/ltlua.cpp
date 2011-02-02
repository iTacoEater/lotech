/* Copyright (C) 2010 Ian MacLarty */

#include <string.h>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

#include "Box2D/Box2D.h"
#include "ltgraphics.h"
#include "ltharness.h"
#include "ltimage.h"
#include "ltlua.h"
#include "ltphysics.h"

#define LT_USERDATA_MT "ltud"

static lua_State *g_L = NULL;

static LTfloat g_screen_w = 1024.0f;
static LTfloat g_screen_h = 768.0f;

static LTfloat g_viewport_x1 = -15.0f;
static LTfloat g_viewport_y1 = -10.0f;
static LTfloat g_viewport_x2 = 15.0f;
static LTfloat g_viewport_y2 = 10.0f;

/************************* General **************************/

// Returns a weak reference to value at the given index.  Does not
// modify stack.
static int make_weak_ref(lua_State *L, int index) {
    lua_getglobal(L, "lt");
    lua_getfield(L, -1, "wrefs");
    if (index > 0) {
        lua_pushvalue(L, index);
    } else {
        lua_pushvalue(L, index - 2);
    }
    int ref = luaL_ref(L, -2);
    lua_pop(L, 2); // pop lt and wrefs.
    return ref;
}

// Pushes referenced value.
static void get_weak_ref(lua_State *L, int ref) {
    lua_getglobal(L, "lt");
    lua_getfield(L, -1, "wrefs");
    lua_rawgeti(L, -1, ref);
    lua_remove(L, -2); // remove wrefs.
    lua_remove(L, -2); // remove lt.
}

// Extract LTObject from wrapper table at the given index.
// Does not modify stack.
static LTObject* get_object(lua_State *L, int index, LTType type) {
    lua_rawgeti(L, index, 1);
    LTObject **ud = (LTObject**)luaL_checkudata(L, -1, LT_USERDATA_MT);
    lua_pop(L, 1);
    if (ud == NULL) {
        luaL_error(L, "ud == NULL.");
    }
    LTObject *o = *ud;
    if (o == NULL) {
        luaL_error(L, "o == NULL.");
    }
    if (!o->hasType(type)) {
        luaL_typerror(L, index, ltTypeName(type));
    }
    return o;
}

static int lt_SetObjectField(lua_State *L) {
    const char *fname;
    LTfloat val;
    LTObject *obj = get_object(L, 1, LT_TYPE_OBJECT);
    // Only allow setting string fields.
    if (lua_isstring(L, 2)) {
        // First try to set the field on the LTObject.
        if (lua_isnumber(L, 3)) {
            fname = luaL_checkstring(L, 2);
            val = (LTfloat)luaL_checknumber(L, 3);
            LTfloat *f = obj->field_ptr(fname);
            if (f != NULL) {
                *f = val;
                return 0;
            }
        }
        // Otherwise set the field in the wrapper table.
        get_weak_ref(L, obj->lua_wrap);
        lua_pushvalue(L, 2);
        lua_pushvalue(L, 3);
        lua_rawset(L, -3);
        return 0;
    } else {
        luaL_error(L, "Attempt to set non-string field.");
    }
    return 0;
}
    
static int lt_SetObjectFields(lua_State *L) {
    const char *fname;
    LTfloat val;
    LTObject *obj = get_object(L, 1, LT_TYPE_OBJECT);
    lua_pushnil(L);
    while (lua_next(L, 2) != 0) {
        fname = luaL_checkstring(L, -2);
        val = (LTfloat)luaL_checknumber(L, -1);
        LTfloat *f = obj->field_ptr(fname);
        if (f != NULL) {
            *f = val;
        }
        lua_pop(L, 1);
    }
    return 0;
}

static int lt_GetObjectField(lua_State *L) {
    LTObject *obj = get_object(L, 1, LT_TYPE_OBJECT);
    const char *fname = luaL_checkstring(L, 2);
    LTfloat *f = obj->field_ptr(fname);
    if (f != NULL) {
        lua_pushnumber(L, *f);
        return 1;
    } else {
        lua_pushnil(L);
        return 1;
    }
}

static int delete_object(lua_State *L) {
    LTObject **ud = (LTObject**)lua_touserdata(L, 1);
    delete (*ud);
    *ud = NULL;
    return 0;
}

// Pushes the wrapper table for the given object.
// If the object has no wrapper table yet, then a new table
// is created.
static void push_wrap(lua_State *L, LTObject *obj) {
    if (obj->lua_wrap != LUA_NOREF) {
        // The object already has a wrapper, so push it and return.
        get_weak_ref(L, obj->lua_wrap);
        return;
    }
    lua_newtable(L);
    lua_getglobal(L, "lt");
    lua_getfield(L, -1, "metatables");
    lua_getfield(L, -1, ltTypeName(obj->type));
    lua_setmetatable(L, -4);
    lua_pop(L, 2); // pop lt, metatables. wrapper table now on top.
    // Push user data for C++ obj.
    LTObject **ud = (LTObject **)lua_newuserdata(L, sizeof(LTObject *));
    *ud = obj;
    // Add metatable for userdata with gc finalizer.
    if (luaL_newmetatable(L, LT_USERDATA_MT)) {
        lua_pushcfunction(L, delete_object);
        lua_setfield(L, -2, "__gc");
    }
    lua_setmetatable(L, -2);
    // Set key 1 in wrapper table to user data.
    lua_rawseti(L, -2, 1);
    // Wrapper table should now be on the top of the stack.
    obj->lua_wrap = make_weak_ref(L, -1);
}

// Inserts the object at the given index into the wrapper
// table at the given index so that the GC can trace it.
static void add_ref(lua_State *L, int wrap_index, int ref_index) {
    lua_pushvalue(L, ref_index);
    lua_pushboolean(L, 1);
    if (wrap_index > 0) {
        lua_rawset(L, wrap_index);
    } else {
        lua_rawset(L, wrap_index - 2);
    }
}

// Removes the object at the given index from the wrapper
// table.
static void del_ref(lua_State *L, int wrap_index, int ref_index) {
    lua_pushvalue(L, ref_index);
    lua_pushnil(L);
    if (wrap_index > 0) {
        lua_rawset(L, wrap_index);
    } else {
        lua_rawset(L, wrap_index - 2);
    }
}

static int check_nargs(lua_State *L, int exp_args) {
    int n = lua_gettop(L);
    if (n < exp_args) {
        return luaL_error(L, "Expecting at least %d args, got %d.", exp_args, n);
    } else {
        return n;
    }
}

/************************* Graphics **************************/

static int lt_SetViewPort(lua_State *L) {
    check_nargs(L, 4);
    g_viewport_x1 = (LTfloat)luaL_checknumber(L, 1);
    g_viewport_y1 = (LTfloat)luaL_checknumber(L, 2);
    g_viewport_x2 = (LTfloat)luaL_checknumber(L, 3);
    g_viewport_y2 = (LTfloat)luaL_checknumber(L, 4);
    ltSetViewPort(g_viewport_x1, g_viewport_y1, g_viewport_x2, g_viewport_y2);
    return 0;
}

static int lt_PushTint(lua_State *L) {
    int num_args = check_nargs(L, 3);
    LTfloat r = (LTfloat)luaL_checknumber(L, 1);
    LTfloat g = (LTfloat)luaL_checknumber(L, 2);
    LTfloat b = (LTfloat)luaL_checknumber(L, 3);
    LTfloat a;
    if (num_args > 3) {
        a = (LTfloat)luaL_checknumber(L, 4);
    } else {
        a = 1.0f;
    }
    ltPushTint(r, g, b, a);
    return 0;
}

static int lt_PopTint(lua_State *L) {
    ltPopTint();
    return 0;
}

static int lt_PushMatrix(lua_State *L) {
    ltPushMatrix();
    return 0;
}

static int lt_PopMatrix(lua_State *L) {
    ltPopMatrix();
    return 0;
}

static int lt_DrawUnitSquare(lua_State *L) {
    ltDrawUnitSquare();
    return 0;
}

static int lt_DrawUnitCircle(lua_State *L) {
    ltDrawUnitCircle();
    return 0;
}

static int lt_DrawRect(lua_State *L) {
    check_nargs(L, 4);
    LTfloat x1 = (LTfloat)luaL_checknumber(L, 1);
    LTfloat y1 = (LTfloat)luaL_checknumber(L, 2);
    LTfloat x2 = (LTfloat)luaL_checknumber(L, 3);
    LTfloat y2 = (LTfloat)luaL_checknumber(L, 4);
    ltDrawRect(x1, y1, x2, y2);
    return 0;
}

static int lt_DrawEllipse(lua_State *L) {
    check_nargs(L, 4);
    LTfloat x = (LTfloat)luaL_checknumber(L, 1);
    LTfloat y = (LTfloat)luaL_checknumber(L, 2);
    LTfloat rx = (LTfloat)luaL_checknumber(L, 3);
    LTfloat ry = (LTfloat)luaL_checknumber(L, 4);
    ltDrawEllipse(x, y, rx, ry);
    return 0;
}

static int lt_DrawSceneNode(lua_State *L) {
    check_nargs(L, 1);
    LTSceneNode *node = (LTSceneNode*)get_object(L, 1, LT_TYPE_SCENENODE);
    node->draw();
    return 0;
}

static int lt_InsertIntoLayer(lua_State *L) {
    int num_args = check_nargs(L, 2);
    LTLayer *layer = (LTLayer*)get_object(L, 1, LT_TYPE_LAYER);
    LTSceneNode *node = (LTSceneNode*)get_object(L, 2, LT_TYPE_SCENENODE);
    LTfloat depth = 0.0f;
    if (num_args > 2) {
        depth = luaL_checknumber(L, 3);
    }
    layer->insert(node, depth);
    add_ref(L, 1, 2);
    return 0;
}

static int lt_RemoveFromLayer(lua_State *L) {
    check_nargs(L, 2);
    LTLayer *layer = (LTLayer*)get_object(L, 1, LT_TYPE_LAYER);
    LTSceneNode *node = (LTSceneNode*)get_object(L, 2, LT_TYPE_SCENENODE);
    layer->remove(node);
    del_ref(L, 1, 2);
    return 0;
}

static int lt_Layer(lua_State *L) {
    LTLayer *layer = new LTLayer();
    push_wrap(L, layer);
    return 1;
}

static int lt_Translate(lua_State *L) {
    check_nargs(L, 3);
    LTSceneNode *target = (LTSceneNode *)get_object(L, 1, LT_TYPE_SCENENODE);
    LTfloat x = (LTfloat)luaL_checknumber(L, 2);
    LTfloat y = (LTfloat)luaL_checknumber(L, 3);
    LTTranslateNode *node = new LTTranslateNode(x, y, target);
    push_wrap(L, node);
    add_ref(L, -1, 1); // Add reference from new node to target.
    return 1;
}

static int lt_Rotate(lua_State *L) {
    check_nargs(L, 2);
    LTSceneNode *target = (LTSceneNode *)get_object(L, 1, LT_TYPE_SCENENODE);
    LTdegrees angle = (LTfloat)luaL_checknumber(L, 2);
    LTRotateNode *node = new LTRotateNode(angle, target);
    push_wrap(L, node);
    add_ref(L, -1, 1); // Add reference from new node to target.
    return 1;
}

static int lt_Scale(lua_State *L) {
    int num_args = check_nargs(L, 2);
    LTSceneNode *target = (LTSceneNode *)get_object(L, 1, LT_TYPE_SCENENODE);
    LTfloat sx = (LTfloat)luaL_checknumber(L, 2);
    LTfloat sy = sx;
    if (num_args > 2) {
        sy = (LTfloat)luaL_checknumber(L, 3);
    }
    LTScaleNode *node = new LTScaleNode(sx, sy, target);
    push_wrap(L, node);
    add_ref(L, -1, 1); // Add reference from new node to target.
    return 1;
}

static int lt_Tint(lua_State *L) {
    int num_args = check_nargs(L, 4);
    LTSceneNode *target = (LTSceneNode *)get_object(L, 1, LT_TYPE_SCENENODE);
    LTfloat r = (LTfloat)luaL_checknumber(L, 2);
    LTfloat g = (LTfloat)luaL_checknumber(L, 3);
    LTfloat b = (LTfloat)luaL_checknumber(L, 4);
    LTfloat a = 1.0f;
    if (num_args > 4) {
        a = (LTfloat)luaL_checknumber(L, 5);
    }
    LTTintNode *tinter = new LTTintNode(r, g, b, a, target);
    push_wrap(L, tinter);
    add_ref(L, -1, 1); // Add reference from new node to target.
    return 1;
}

static int lt_Line(lua_State *L) {
    check_nargs(L, 4);
    LTfloat x1 = (LTfloat)luaL_checknumber(L, 1);
    LTfloat y1 = (LTfloat)luaL_checknumber(L, 2);
    LTfloat x2 = (LTfloat)luaL_checknumber(L, 3);
    LTfloat y2 = (LTfloat)luaL_checknumber(L, 4);
    LTLineNode *line = new LTLineNode(x1, y1, x2, y2);
    push_wrap(L, line);
    return 1;
}

static int lt_Triangle(lua_State *L) {
    check_nargs(L, 6);
    LTfloat x1 = (LTfloat)luaL_checknumber(L, 1);
    LTfloat y1 = (LTfloat)luaL_checknumber(L, 2);
    LTfloat x2 = (LTfloat)luaL_checknumber(L, 3);
    LTfloat y2 = (LTfloat)luaL_checknumber(L, 4);
    LTfloat x3 = (LTfloat)luaL_checknumber(L, 5);
    LTfloat y3 = (LTfloat)luaL_checknumber(L, 6);
    LTTriangleNode *triangle = new LTTriangleNode(x1, y1, x2, y2, x3, y3);
    push_wrap(L, triangle);
    return 1;
}

static int lt_Rect(lua_State *L) {
    check_nargs(L, 4);
    LTfloat x1 = (LTfloat)luaL_checknumber(L, 1);
    LTfloat y1 = (LTfloat)luaL_checknumber(L, 2);
    LTfloat x2 = (LTfloat)luaL_checknumber(L, 3);
    LTfloat y2 = (LTfloat)luaL_checknumber(L, 4);
    LTRectNode *node = new LTRectNode(x1, y1, x2, y2);
    push_wrap(L, node);
    return 1;
}

/************************* Events **************************/

static bool call_pointer_event_handler(lua_State *L, int func, LTfloat x, LTfloat y, int button) {
    get_weak_ref(L, func);
    if (lua_isfunction(L, -1)) {
        lua_pushnumber(L, x);
        lua_pushnumber(L, y);
        lua_pushinteger(L, button);
        lua_call(L, 3, 1);
        bool consumed = lua_toboolean(L, -1);
        lua_pop(L, 1);
        return consumed;
    } else {
        return false;
    }
}

struct LTLPointerDownInEventHandler : LTPointerEventHandler {
    int lua_func_ref;

    LTLPointerDownInEventHandler(int func_index) {
        lua_func_ref = make_weak_ref(g_L, func_index);
    }

    virtual bool consume(LTfloat x, LTfloat y, LTSceneNode *node, LTPointerEvent *event) {
        if (event->type == LT_EVENT_POINTER_DOWN) {
            if (node->containsPoint(x, y)) {
                return call_pointer_event_handler(g_L, lua_func_ref, x, y, event->button);
            } else {
                return false;
            }
        } else {
            return false;
        }
    }
};

struct LTLPointerMoveEventHandler : LTPointerEventHandler {
    int lua_func_ref;

    LTLPointerMoveEventHandler(int func_index) {
        lua_func_ref = make_weak_ref(g_L, func_index);
    }

    virtual bool consume(LTfloat x, LTfloat y, LTSceneNode *node, LTPointerEvent *event) {
        if (event->type == LT_EVENT_POINTER_MOVE) {
            return call_pointer_event_handler(g_L, lua_func_ref, x, y, event->button);
        } else {
            return false;
        }
    }
};

struct LTLPointerOverEventHandler : LTPointerEventHandler {
    int lua_enter_func_ref;
    int lua_exit_func_ref;
    bool first_time;
    bool in;

    LTLPointerOverEventHandler(int enter_func_index, int exit_func_index) {
        lua_enter_func_ref = make_weak_ref(g_L, enter_func_index);
        lua_exit_func_ref = make_weak_ref(g_L, exit_func_index);
        first_time = true;
        in = false;
    }

    virtual bool consume(LTfloat x, LTfloat y, LTSceneNode *node, LTPointerEvent *event) {
        if (event->type == LT_EVENT_POINTER_MOVE) {
            bool containsPoint = node->containsPoint(x, y);
            if (first_time) {
                first_time = false;
                in = containsPoint;
                if (in) {
                    return call_pointer_event_handler(g_L, lua_enter_func_ref, x, y, event->button);
                } else {
                    return false;
                }
            } else {
                bool res = false;
                if (containsPoint && !in) {
                    res = call_pointer_event_handler(g_L, lua_enter_func_ref, x, y, event->button);
                } else if (!containsPoint && in) {
                    res = call_pointer_event_handler(g_L, lua_exit_func_ref, x, y, event->button);
                }
                in = containsPoint;
                return res;
            }
        } else {
            return false;
        }
    }
};

static int lt_AddOnPointerDownHandler(lua_State *L) {
    check_nargs(L, 2);
    LTSceneNode *node = (LTSceneNode*)get_object(L, 1, LT_TYPE_SCENENODE);
    LTLPointerDownInEventHandler *handler = new LTLPointerDownInEventHandler(2);
    node->addHandler(handler);
    add_ref(L, 1, 2);
    return 0;
}

static int lt_AddOnPointerOverHandler(lua_State *L) {
    check_nargs(L, 3);
    LTSceneNode *node = (LTSceneNode*)get_object(L, 1, LT_TYPE_SCENENODE);
    LTLPointerOverEventHandler *handler = new LTLPointerOverEventHandler(2, 3);
    node->addHandler(handler);
    add_ref(L, 1, 2);
    add_ref(L, 1, 3);
    return 0;
}

static int lt_PropogatePointerDownEvent(lua_State *L) {
    check_nargs(L, 4);
    LTSceneNode *node = (LTSceneNode*)get_object(L, 1, LT_TYPE_SCENENODE);
    int button = luaL_checkinteger(L, 2);
    LTfloat x = luaL_checknumber(L, 3);
    LTfloat y = luaL_checknumber(L, 4);
    LTPointerEvent event(LT_EVENT_POINTER_DOWN, x, y, button);
    node->propogatePointerEvent(x, y, &event);
    return 0;
}

static int lt_PropogatePointerMoveEvent(lua_State *L) {
    check_nargs(L, 3);
    LTSceneNode *node = (LTSceneNode*)get_object(L, 1, LT_TYPE_SCENENODE);
    LTfloat x = luaL_checknumber(L, 2);
    LTfloat y = luaL_checknumber(L, 3);
    LTPointerEvent event(LT_EVENT_POINTER_MOVE, x, y, 0);
    node->propogatePointerEvent(x, y, &event);
    return 0;
}

/************************* Images **************************/

static void add_packer_images_to_lua_table(lua_State *L, int w, int h, LTImagePacker *packer, LTAtlas *atlas) {
    char img_name[128];
    int len;
    const char *file;
    if (packer->occupant != NULL) {
        LTImage *img = new LTImage(atlas, w, h, packer);
        file = packer->occupant->file;
        len = strlen(file);
        if (len <= 4) {
            ltAbort("PNG file name too short: %s.", file);
        }
        if (len > 120) {
            ltAbort("PNG file name too long: %s.", file);
        }
        if (strcmp(".png", file + len - 4) != 0) {
            ltAbort("File %s does not end in .png", file);
        }
        strncpy(img_name, file, len - 4); // Remove ".png" suffix.
        img_name[len - 4] = '\0';
        push_wrap(L, img);
        lua_setfield(L, -2, img_name);
        add_packer_images_to_lua_table(L, w, h, packer->lo_child, atlas);
        add_packer_images_to_lua_table(L, w, h, packer->hi_child, atlas);
    }
}

// Load images in 1st argument (an array) and return a table
// indexed by image name (without the .png suffix).
// The second argument is the size of the atlasses to generate
// (1024 by default).
static int lt_LoadImages(lua_State *L) {
    int num_args = check_nargs(L, 1);
    int size = 1024;
    if (num_args > 1) {
        size = (int)luaL_checkinteger(L, 2);
    }
    lua_newtable(L); // The table to be returned.
    LTImagePacker *packer = new LTImagePacker(0, 0, size, size);
    int atlas_num = 1;
    int i = 1;
    do {
        lua_pushinteger(L, i);
        lua_gettable(L, 1);
        if (lua_isnil(L, -1)) {
            // We've reached the end of the array.
            lua_pop(L, 1);
            break;
        }
        const char* file = lua_tostring(L, -1);
        lua_pop(L, 1);
        if (file == NULL) {
            return luaL_error(L, "Expecting an array of strings.");
        }
        
        LTImageBuffer *buf = ltReadImage(file);
        if (!ltPackImage(packer, buf)) {
            // Packer full, so generate an atlas.
            LTAtlas *atlas = new LTAtlas(packer);
            add_packer_images_to_lua_table(L, packer->width, packer->height, packer, atlas);
            packer->deleteOccupants();
            atlas_num++;

            if (!ltPackImage(packer, buf)) {
                return luaL_error(L, "Image %s is too large.", file);
            }
        }

        i++;
    } while (true);

    // Pack any images left in packer into a new texture.
    if (packer->size() > 0) {
        LTAtlas *atlas = new LTAtlas(packer);
        add_packer_images_to_lua_table(L, packer->width, packer->height, packer, atlas);
        packer->deleteOccupants();
        atlas_num++;
    }
        
    delete packer;

    return 1;
}

/************************* Box2D **************************/

static int lt_FixtureContainsPoint(lua_State *L) {
    check_nargs(L, 3); 
    LTFixture *fixture = (LTFixture*)get_object(L, 1, LT_TYPE_FIXTURE);
    LTfloat x = luaL_checknumber(L, 2);
    LTfloat y = luaL_checknumber(L, 3);
    if (fixture->fixture != NULL) {
        lua_pushboolean(L, fixture->fixture->TestPoint(b2Vec2(x, y)));
    } else {
        lua_pushboolean(L, 0);
    }
    return 1;
}

static int lt_DestroyFixture(lua_State *L) {
    check_nargs(L, 1); 
    LTFixture *fixture = (LTFixture*)get_object(L, 1, LT_TYPE_FIXTURE);
    fixture->destroy();
    LTBody *body = fixture->body;
    if (body != NULL) {
        get_weak_ref(L, fixture->lua_wrap);
        get_weak_ref(L, body->lua_wrap);
        del_ref(L, -1, -2); // Remove reference from body to fixture.
        del_ref(L, -2, -1); // Remove reference from fixture to body.
        lua_pop(L, 2);
    }
    return 0;
}

static int lt_FixtureIsDestroyed(lua_State *L) {
    check_nargs(L, 1); 
    LTFixture *fixture = (LTFixture*)get_object(L, 1, LT_TYPE_FIXTURE);
    lua_pushboolean(L, fixture->fixture == NULL);
    return 1;
}

static int lt_DoWorldStep(lua_State *L) {
    int num_args = check_nargs(L, 2); 
    LTWorld *world = (LTWorld*)get_object(L, 1, LT_TYPE_WORLD);
    LTfloat time_step = luaL_checknumber(L, 2);
    int velocity_iterations = 10;
    int position_iterations = 8;
    if (num_args > 2) {
        velocity_iterations = luaL_checkinteger(L, 3);
    }
    if (num_args > 3) {
        position_iterations = luaL_checkinteger(L, 4);
    }
    world->world->Step(time_step, velocity_iterations, position_iterations);
    return 0;
}

static int lt_SetWorldGravity(lua_State *L) {
    check_nargs(L, 3);
    LTWorld *world = (LTWorld*)get_object(L, 1, LT_TYPE_WORLD);
    LTfloat x = (LTfloat)luaL_checknumber(L, 2);
    LTfloat y = (LTfloat)luaL_checknumber(L, 3);
    world->world->SetGravity(b2Vec2(x, y));
    return 0;
}

struct AABBQueryCallBack : b2QueryCallback {
    lua_State *L;
    int i;

    AABBQueryCallBack(lua_State *L) {
        AABBQueryCallBack::L = L;
        i = 1;
    }

    virtual bool ReportFixture(b2Fixture *fixture) {
        LTFixture *f = (LTFixture*)fixture->GetUserData();
        push_wrap(L, f);
        lua_rawseti(L, -2, i);
        i++;
        return true;
    }
};

static int lt_WorldQueryBox(lua_State *L) {
    check_nargs(L, 5);
    LTWorld *world = (LTWorld*)get_object(L, 1, LT_TYPE_WORLD);
    LTfloat x1 = (LTfloat)luaL_checknumber(L, 2);
    LTfloat y1 = (LTfloat)luaL_checknumber(L, 3);
    LTfloat x2 = (LTfloat)luaL_checknumber(L, 4);
    LTfloat y2 = (LTfloat)luaL_checknumber(L, 5);
    b2AABB aabb;
    if (x1 > x2) {
        aabb.upperBound.x = x1;
        aabb.lowerBound.x = x2;
    } else {
        aabb.upperBound.x = x2;
        aabb.lowerBound.x = x1;
    }
    if (y1 > y2) {
        aabb.upperBound.y = y1;
        aabb.lowerBound.y = y2;
    } else {
        aabb.upperBound.y = y2;
        aabb.lowerBound.y = y1;
    }
    AABBQueryCallBack cb(L);
    lua_newtable(L);
    world->world->QueryAABB(&cb, aabb);
    return 1;
}

static int lt_DestroyBody(lua_State *L) {
    check_nargs(L, 1);
    LTBody *body = (LTBody*)get_object(L, 1, LT_TYPE_BODY);
    body->destroy();
    b2Body *b = body->body;
    if (b != NULL) {
        // Remove references between body and its fixtures.
        b2Fixture *f = b->GetFixtureList();
        while (f != NULL) {
            LTFixture *ud = (LTFixture*)f->GetUserData();
            get_weak_ref(L, ud->lua_wrap);
            del_ref(L, 1, -1); // Remove reference from body to fixture.
            del_ref(L, -1, 1); // Remove reference from fixture to body.
            lua_pop(L, 1);
            f = f->GetNext();
        }
    }
    LTWorld *world = body->world;
    get_weak_ref(L, world->lua_wrap);
    del_ref(L, -1, 1); // Remove reference from world to body.
    del_ref(L, 1, -1); // Remove reference from body to world.
    return 0;
}

static int lt_BodyIsDestroyed(lua_State *L) {
    check_nargs(L, 1);
    LTBody *body = (LTBody*)get_object(L, 1, LT_TYPE_BODY);
    lua_pushboolean(L, body->body == NULL);
    return 1;
}

static int lt_ApplyForceToBody(lua_State *L) {
    int num_args = check_nargs(L, 3);
    LTBody *body = (LTBody*)get_object(L, 1, LT_TYPE_BODY);
    if (body->body != NULL) {
        b2Vec2 force;
        b2Vec2 pos;
        force.x = luaL_checknumber(L, 2);
        force.y = (LTfloat)luaL_checknumber(L, 3);
        if (num_args >= 5) {
            pos.x = (LTfloat)luaL_checknumber(L, 4);
            pos.y = (LTfloat)luaL_checknumber(L, 5);
        } else {
            pos = body->body->GetWorldCenter();
        }
        body->body->ApplyForce(force, pos);
    }
    return 0;
}

static int lt_ApplyTorqueToBody(lua_State *L) {
    check_nargs(L, 2);
    LTBody *body = (LTBody*)get_object(L, 1, LT_TYPE_BODY);
    if (body->body != NULL) {
        body->body->ApplyTorque(luaL_checknumber(L, 2));
    }
    return 0;
}

static int lt_GetBodyAngle(lua_State *L) {
    check_nargs(L, 1);
    LTBody *body = (LTBody*)get_object(L, 1, LT_TYPE_BODY);
    if (body->body != NULL) {
        lua_pushnumber(L, body->body->GetAngle() * LT_DEGREES_PER_RADIAN);
    } else {
        lua_pushnil(L);
    }
    return 1;
}

static int lt_SetBodyAngle(lua_State *L) {
    check_nargs(L, 2);
    LTBody *body = (LTBody*)get_object(L, 1, LT_TYPE_BODY);
    if (body->body != NULL) {
        body->body->SetTransform(body->body->GetPosition(), luaL_checknumber(L, 2) * LT_RADIANS_PER_DEGREE);
    } else {
        lua_pushnil(L);
    }
    return 1;
}

static int lt_GetBodyPosition(lua_State *L) {
    check_nargs(L, 1);
    LTBody *body = (LTBody*)get_object(L, 1, LT_TYPE_BODY);
    if (body->body != NULL) {
        b2Vec2 pos = body->body->GetPosition();
        lua_pushnumber(L, pos.x);
        lua_pushnumber(L, pos.y);
        return 2;
    }
    return 0;
}

static int lt_SetBodyAngularVelocity(lua_State *L) {
    check_nargs(L, 2);
    LTBody *body = (LTBody*)get_object(L, 1, LT_TYPE_BODY);
    if (body->body != NULL) {
        body->body->SetAngularVelocity(luaL_checknumber(L, 2) * LT_RADIANS_PER_DEGREE);
    }
    return 0;
}

static int lt_AddRectToBody(lua_State *L) {
    int num_args = check_nargs(L, 5);
    LTBody *body = (LTBody*)get_object(L, 1, LT_TYPE_BODY);
    if (body->body != NULL) {
        LTfloat x1 = (LTfloat)luaL_checknumber(L, 2);
        LTfloat y1 = (LTfloat)luaL_checknumber(L, 3);
        LTfloat x2 = (LTfloat)luaL_checknumber(L, 4);
        LTfloat y2 = (LTfloat)luaL_checknumber(L, 5);
        LTfloat density = 0.0f;
        if (num_args > 5) {
            density = (LTfloat)luaL_checknumber(L, 6);
        }
        b2PolygonShape poly;
        poly.m_vertexCount = 4;
        poly.m_vertices[0].Set(x1, y1);
        poly.m_vertices[1].Set(x2, y1);
        poly.m_vertices[2].Set(x2, y2);
        poly.m_vertices[3].Set(x1, y2);
        poly.m_normals[0].Set(0.0f, -1.0f);
        poly.m_normals[1].Set(1.0f, 0.0f);
        poly.m_normals[2].Set(0.0f, 1.0f);
        poly.m_normals[3].Set(-1.0f, 0.0f);
        poly.m_centroid.Set(x1 + ((x2 - x1) * 0.5f), y1 + ((y2 - y1) * 0.5f));
        b2FixtureDef fixtureDef;
        fixtureDef.density = density;
        fixtureDef.shape = &poly;
        LTFixture *fixture = new LTFixture(body, &fixtureDef);
        push_wrap(L, fixture);
        add_ref(L, 1, -1); // Add reference from body to new fixture.
        add_ref(L, -1, 1); // Add reference from fixture to body.
    } else {
        lua_pushnil(L);
    }
    return 1;
}

static int lt_AddTriangleToBody(lua_State *L) {
    int num_args = check_nargs(L, 7);
    LTBody *body = (LTBody*)get_object(L, 1, LT_TYPE_BODY);
    if (body->body != NULL) {
        LTfloat x1 = (LTfloat)luaL_checknumber(L, 2);
        LTfloat y1 = (LTfloat)luaL_checknumber(L, 3);
        LTfloat x2 = (LTfloat)luaL_checknumber(L, 4);
        LTfloat y2 = (LTfloat)luaL_checknumber(L, 5);
        LTfloat x3 = (LTfloat)luaL_checknumber(L, 6);
        LTfloat y3 = (LTfloat)luaL_checknumber(L, 7);
        LTfloat density = 0.0f;
        if (num_args > 7) {
            density = (LTfloat)luaL_checknumber(L, 8);
        }
        b2PolygonShape poly;
        b2Vec2 vertices[3];
        vertices[0].Set(x1, y1);
        vertices[1].Set(x2, y2);
        vertices[2].Set(x3, y3);
        if (!ltCheckB2Poly(vertices, 3)) {
            vertices[2] = vertices[0];
            vertices[0].Set(x3, y3);
            if (!ltCheckB2Poly(vertices, 3)) {
                lua_pushnil(L);
                return 1;
            }
        }
        poly.Set(vertices, 3);
        b2FixtureDef fixtureDef;
        fixtureDef.density = density;
        fixtureDef.shape = &poly;
        LTFixture *fixture = new LTFixture(body, &fixtureDef);
        push_wrap(L, fixture);
        add_ref(L, 1, -1); // Add reference from body to new fixture.
        add_ref(L, -1, 1); // Add reference from fixture to body.
    } else {
        lua_pushnil(L);
    }
    return 1;
}

static int lt_GetFixtureBody(lua_State *L) {
    check_nargs(L, 1);
    LTFixture *fixture = (LTFixture*)get_object(L, 1, LT_TYPE_FIXTURE);
    if (fixture->fixture != NULL && fixture->body != NULL) {
        push_wrap(L, fixture->body);
    } else {
        lua_pushnil(L);
    }
    return 1;
}

static int lt_AddStaticBodyToWorld(lua_State *L) {
    check_nargs(L, 1);
    LTWorld *world = (LTWorld*)get_object(L, 1, LT_TYPE_WORLD);
    b2BodyDef def;
    def.type = b2_staticBody;
    LTBody *body = new LTBody(world, &def);
    push_wrap(L, body);
    add_ref(L, 1, -1); // Add reference from world to body.
    add_ref(L, -1, 1); // Add reference from body to world.
    return 1;
}

static int lt_AddDynamicBodyToWorld(lua_State *L) {
    int num_args = check_nargs(L, 3);
    LTWorld *world = (LTWorld*)get_object(L, 1, LT_TYPE_WORLD);
    LTfloat x = (LTfloat)luaL_checknumber(L, 2);
    LTfloat y = (LTfloat)luaL_checknumber(L, 3);
    LTfloat angle = 0.0f;
    if (num_args > 3) {
        angle = (LTfloat)luaL_checknumber(L, 4);
    }
    b2BodyDef def;
    def.type = b2_dynamicBody;
    def.position.Set(x, y);
    def.angle = angle;
    LTBody *body = new LTBody(world, &def);
    push_wrap(L, body);
    add_ref(L, 1, -1); // Add reference from world to body.
    add_ref(L, -1, 1); // Add reference from body to world.
    return 1;
}

static int lt_World(lua_State *L) {
    LTWorld *world = new LTWorld(b2Vec2(0.0f, -10.0f), true);
    push_wrap(L, world);
    return 1;
}

/************************************************************/

static const luaL_Reg ltlib[] = {
    {"GetObjectField",          lt_GetObjectField},
    {"SetObjectField",          lt_SetObjectField},
    {"SetObjectFields",         lt_SetObjectFields},

    {"SetViewPort",             lt_SetViewPort},
    {"PushTint",                lt_PushTint},
    {"PopTint",                 lt_PopTint},
    {"PushMatrix",              lt_PushMatrix},
    {"PopMatrix",               lt_PopMatrix},
    {"DrawUnitSquare",          lt_DrawUnitSquare},
    {"DrawUnitCircle",          lt_DrawUnitCircle},
    {"DrawRect",                lt_DrawRect},
    {"DrawEllipse",             lt_DrawEllipse},

    {"Layer",                   lt_Layer},
    {"Line",                    lt_Line},
    {"Triangle",                lt_Triangle},
    {"Rect",                    lt_Rect},
    {"Tint",                    lt_Tint},
    {"Scale",                   lt_Scale},
    {"Translate",               lt_Translate},
    {"Rotate",                  lt_Rotate},

    {"DrawSceneNode",           lt_DrawSceneNode},
    {"AddOnPointerDownHandler", lt_AddOnPointerDownHandler},
    {"AddOnPointerOverHandler", lt_AddOnPointerOverHandler},
    {"PropogatePointerDownEvent",lt_PropogatePointerDownEvent},
    {"PropogatePointerMoveEvent",lt_PropogatePointerMoveEvent},
    {"InsertIntoLayer",         lt_InsertIntoLayer},
    {"RemoveFromLayer",         lt_RemoveFromLayer},

    {"LoadImages",              lt_LoadImages},

    {"World",                   lt_World},
    {"FixtureContainsPoint",    lt_FixtureContainsPoint},
    {"DestroyFixture",          lt_DestroyFixture},
    {"FixtureIsDestroyed",      lt_FixtureIsDestroyed},
    {"DoWorldStep",             lt_DoWorldStep},
    {"SetWorldGravity",         lt_SetWorldGravity},
    {"WorldQueryBox",           lt_WorldQueryBox},
    {"DestroyBody",             lt_DestroyBody},
    {"BodyIsDestroyed",         lt_BodyIsDestroyed},
    {"ApplyForceToBody",        lt_ApplyForceToBody},
    {"ApplyTorqueToBody",       lt_ApplyTorqueToBody},
    {"GetBodyAngle",            lt_GetBodyAngle},
    {"SetBodyAngle",            lt_SetBodyAngle},
    {"GetBodyPosition" ,        lt_GetBodyPosition},
    {"SetBodyAngularVelocity",  lt_SetBodyAngularVelocity},
    {"AddRectToBody",           lt_AddRectToBody},
    {"AddTriangleToBody",       lt_AddTriangleToBody},
    {"GetFixtureBody",          lt_GetFixtureBody},
    {"AddStaticBodyToWorld",    lt_AddStaticBodyToWorld},
    {"AddDynamicBodyToWorld",   lt_AddDynamicBodyToWorld},

    {NULL, NULL}
};

/************************************************************/

static void check_status(int status, bool abort) {
    if (status && !lua_isnil(g_L, -1)) {
        const char *msg = lua_tostring(g_L, -1);
        if (msg == NULL) msg = "(error object is not a string)";
        fprintf(stderr, "%s\n", msg);
        lua_pop(g_L, 1);
        if (abort) {
            ltHarnessQuit();
        }
    }
}

static bool push_lt_func(const char *func) {
    lua_getglobal(g_L, "lt");
    if (lua_istable(g_L, -1)) {
        lua_getfield(g_L, -1, func);
        lua_remove(g_L, -2); // Remove lt table.
        if (lua_isfunction(g_L, -1)) {
            return true;
        } else {
            lua_pop(g_L, 1); // Pop the field since we won't be calling it.
            return false;
        }
    } else {
        lua_pop(g_L, 1);
        return false;
    }
}

static void call_lt_func(const char *func) {
    if (push_lt_func(func)) {
        check_status(lua_pcall(g_L, 0, 0, 0), true);
    }
}

void ltLuaSetup(const char *file) {
    g_L = luaL_newstate();
    if (g_L == NULL) {
        fprintf(stderr, "Cannot create lua state: not enough memory.\n");
        exit(1);
    }
    lua_gc(g_L, LUA_GCSTOP, 0);  /* stop collector during library initialization */
    luaL_openlibs(g_L);
    luaL_register(g_L, "lt", ltlib);
    lua_gc(g_L, LUA_GCRESTART, 0);
    check_status(luaL_loadfile(g_L, file), true);
    check_status(lua_pcall(g_L, 0, 0, 0), true);
}

void ltLuaTeardown() {
    if (g_L != NULL) {
        lua_close(g_L);
        g_L = NULL;
    }
}

void ltLuaAdvance() {
    if (g_L != NULL) {
        call_lt_func("Advance");
    }
}

void ltLuaRender() {
    if (g_L != NULL) {
        call_lt_func("Render");
    }
}

static const char *lt_key_str(LTKey key) {
    switch (key) {
        case LT_KEY_0: return "0"; 
        case LT_KEY_1: return "1"; 
        case LT_KEY_2: return "2"; 
        case LT_KEY_3: return "3"; 
        case LT_KEY_4: return "4"; 
        case LT_KEY_5: return "5"; 
        case LT_KEY_6: return "6"; 
        case LT_KEY_7: return "7"; 
        case LT_KEY_8: return "8"; 
        case LT_KEY_9: return "9"; 
        case LT_KEY_A: return "A"; 
        case LT_KEY_B: return "B"; 
        case LT_KEY_C: return "C"; 
        case LT_KEY_D: return "D"; 
        case LT_KEY_E: return "E"; 
        case LT_KEY_F: return "F"; 
        case LT_KEY_G: return "G"; 
        case LT_KEY_H: return "H"; 
        case LT_KEY_I: return "I"; 
        case LT_KEY_J: return "J"; 
        case LT_KEY_K: return "K"; 
        case LT_KEY_L: return "L"; 
        case LT_KEY_M: return "M"; 
        case LT_KEY_N: return "N"; 
        case LT_KEY_O: return "O"; 
        case LT_KEY_P: return "P"; 
        case LT_KEY_Q: return "Q"; 
        case LT_KEY_R: return "R"; 
        case LT_KEY_S: return "S"; 
        case LT_KEY_T: return "T"; 
        case LT_KEY_U: return "U"; 
        case LT_KEY_V: return "V"; 
        case LT_KEY_W: return "W"; 
        case LT_KEY_X: return "X"; 
        case LT_KEY_Y: return "Y"; 
        case LT_KEY_Z: return "Z"; 
        case LT_KEY_SPACE: return "space"; 
        case LT_KEY_TAB: return "tab"; 
        case LT_KEY_ENTER: return "enter"; 
        case LT_KEY_UP: return "up"; 
        case LT_KEY_DOWN: return "down"; 
        case LT_KEY_LEFT: return "left"; 
        case LT_KEY_RIGHT: return "right"; 
        case LT_KEY_RIGHT_BRACKET: return "["; 
        case LT_KEY_LEFT_BRACKET: return "]"; 
        case LT_KEY_BACKSLASH: return "\\"; 
        case LT_KEY_SEMI_COLON: return ":"; 
        case LT_KEY_APOS: return ";"; 
        case LT_KEY_COMMA: return ","; 
        case LT_KEY_PERIOD: return "."; 
        case LT_KEY_PLUS: return "+"; 
        case LT_KEY_MINUS: return "-"; 
        case LT_KEY_TICK: return "`"; 
        case LT_KEY_DEL: return "del"; 
        case LT_KEY_UNKNOWN: return "unknown";
    }
    return "";
}

void ltLuaKeyDown(LTKey key) {
    if (g_L != NULL && push_lt_func("KeyDown")) {
        const char *str = lt_key_str(key);
        lua_pushstring(g_L, str);
        check_status(lua_pcall(g_L, 1, 0, 0), true);
    }
}

void ltLuaKeyUp(LTKey key) {
    if (g_L != NULL && push_lt_func("KeyUp")) {
        const char *str = lt_key_str(key);
        lua_pushstring(g_L, str);
        check_status(lua_pcall(g_L, 1, 0, 0), true);
    }
}

static LTfloat viewport_x(LTfloat screen_x) {
    return g_viewport_x1 + (screen_x / g_screen_w) * (g_viewport_x2 - g_viewport_x1);
}

static LTfloat viewport_y(LTfloat screen_y) {
    return g_viewport_y2 - (screen_y / g_screen_h) * (g_viewport_y2 - g_viewport_y1);
}

void ltLuaMouseDown(int button, LTfloat x, LTfloat y) {
    if (g_L != NULL && push_lt_func("MouseDown")) {
        lua_pushinteger(g_L, button);
        lua_pushnumber(g_L, viewport_x(x));
        lua_pushnumber(g_L, viewport_y(y));
        check_status(lua_pcall(g_L, 3, 0, 0), true);
    }
}

void ltLuaMouseUp(int button, LTfloat x, LTfloat y) {
    if (g_L != NULL && push_lt_func("MouseUp")) {
        lua_pushinteger(g_L, button);
        lua_pushnumber(g_L, viewport_x(x));
        lua_pushnumber(g_L, viewport_y(y));
        check_status(lua_pcall(g_L, 3, 0, 0), true);
    }
}

void ltLuaMouseMove(LTfloat x, LTfloat y) {
    if (g_L != NULL && push_lt_func("MouseMove")) {
        lua_pushnumber(g_L, viewport_x(x));
        lua_pushnumber(g_L, viewport_y(y));
        check_status(lua_pcall(g_L, 2, 0, 0), true);
    }
}

void ltLuaResizeWindow(LTfloat w, LTfloat h) {
    g_screen_w = w;
    g_screen_h = h;
}

/************************************************************/

int ltLuaInitRef() {
    return LUA_NOREF;
}
