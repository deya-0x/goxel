/* Goxel 3D voxels editor
 *
 * copyright (c) 2018 Guillaume Chereau <guillaume@noctua-software.com>
 *
 * Goxel is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.

 * Goxel is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.

 * You should have received a copy of the GNU General Public License along with
 * goxel.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "goxel.h"

static const uint8_t FACES_COLOR[6][3] = {
    {0, 255, 0},
    {0, 255, 0},
    {0, 0, 255},
    {0, 0, 255},
    {255, 0, 0},
    {255, 0, 0},
};

enum {
    FLAG_SNAPED     = 1 << 0,
    FLAG_MOVING     = 1 << 1,
    FLAG_FIRST      = 1 << 2,
};

typedef struct data
{
    int mode; // 0: move, 1: resize.
    float box[4][4];
    float start_box[4][4];
    float transf[4][4];
    int snap_face;
    int flags;
} data_t;

static data_t g_data = {};

// Get the face index from the normal.
static int get_face(const float n[3])
{
    int f;
    const int *n2;
    for (f = 0; f < 6; f++) {
        n2 = FACES_NORMALS[f];
        if (vec3_dot(n, VEC(n2[0], n2[1], n2[2])) > 0.5)
            return f;
    }
    return -1;
}

static void get_transf(const float src[4][4], const float dst[4][4],
                       float out[4][4])
{
    mat4_invert(src, out);
    mat4_mul(dst, out, out);
}

static void render_gizmo(const float box[4][4], int face)
{
    float plane[4][4];
    uint8_t color[4] = {0, 0, 0, 100};
    float a[3], b[3], dir[3];

    mat4_mul(box, FACES_MATS[face], plane);
    memcpy(color, FACES_COLOR[face], 3);
    vec3_normalize(plane[2], dir);
    vec3_copy(plane[3], a);
    vec3_addk(a, dir, 3, b);
    render_line(&goxel.rend, a, b, color, EFFECT_ARROW | EFFECT_NO_DEPTH_TEST);
}

static void highlight_face(const float box[4][4], int face)
{
    float plane[4][4];
    uint8_t color[4] = {0, 0, 0, 16};

    mat4_mul(box, FACES_MATS[face], plane);
    mat4_iscale(plane, 2, 2, 1);
    mat4_itranslate(plane, 0, 0, 0.001);
    memcpy(color, FACES_COLOR[face], 3);
    render_rect_fill(&goxel.rend, plane, color);
}

static int on_hover(gesture3d_t *gest, const cursor_t *curs, void *user)
{
    data_t *data = (void*)user;

    goxel_set_help_text("Drag to move face");
    data->flags |= FLAG_SNAPED;
    data->snap_face = get_face(curs->normal);
    highlight_face(data->box, data->snap_face);
    render_gizmo(data->box, data->snap_face);
    return 0;
}

static int on_drag(gesture3d_t *gest, const cursor_t *curs, void *user)
{
    data_t *data = (void*)user;
    float face_plane[4][4], v[3], pos[3], n[3], d[3], ofs[3], box[4][4];

    goxel_set_help_text("Drag to move face");
    data->flags |= FLAG_MOVING;

    if (gest->state == GESTURE_BEGIN) {
        data->flags |= FLAG_FIRST;
        mat4_copy(data->box, data->start_box);
        data->snap_face = get_face(curs->normal);
        mat4_mul(data->box, FACES_MATS[data->snap_face], face_plane);
        vec3_normalize(face_plane[0], v);
        gest->snap_mask = SNAP_SHAPE_PLANE;
        plane_from_vectors(gest->snap_shape, curs->pos, curs->normal, v);
        return 0;
    }

    mat4_mul(data->start_box, FACES_MATS[data->snap_face], face_plane);
    vec3_normalize(face_plane[2], n);
    vec3_sub(curs->pos, gest->snap_shape[3], v);
    vec3_project(v, n, v);
    vec3_add(gest->snap_shape[3], v, pos);
    pos[0] = round(pos[0]);
    pos[1] = round(pos[1]);
    pos[2] = round(pos[2]);

    if (data->mode == 1) { // Resize
        box_move_face(data->start_box, data->snap_face, pos, box);
        if (box_get_volume(box) == 0) return 0;
        get_transf(data->box, box, data->transf);
    } else { // Move
        vec3_add(data->box[3], face_plane[2], d);
        vec3_sub(pos, d, ofs);
        vec3_project(ofs, n, ofs);
        mat4_set_identity(data->transf);
        mat4_itranslate(data->transf, ofs[0], ofs[1], ofs[2]);
    }
    return 0;
}

int box_edit(const float box[4][4], int mode, float transf[4][4], bool *first)
{
    int ret;

    if (box_is_null(box)) return 0;
    g_data.mode = mode;
    mat4_copy(box, g_data.box);
    mat4_set_identity(g_data.transf);

    g_data.flags &= ~(FLAG_SNAPED | FLAG_MOVING);

    goxel_gesture3d(&(gesture3d_t) {
        .type = GESTURE_HOVER,
        .snap_mask = SNAP_SHAPE_BOX,
        .snap_shape = MAT4_COPY(box),
        .callback = on_hover,
        .user = &g_data,
    });
    goxel_gesture3d(&(gesture3d_t) {
        .type = GESTURE_DRAG,
        .snap_mask = SNAP_SHAPE_BOX,
        .snap_shape = MAT4_COPY(box),
        .callback = on_drag,
        .user = &g_data,
    });


    render_box(&goxel.rend, box, NULL, EFFECT_STRIP | EFFECT_WIREFRAME);
    if (transf) mat4_copy(g_data.transf, transf);

    ret = g_data.flags & FLAG_MOVING;
    if (first) {
        *first = g_data.flags & FLAG_FIRST;
        g_data.flags &= ~FLAG_FIRST;
    }
    return ret;
}
