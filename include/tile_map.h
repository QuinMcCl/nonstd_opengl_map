#ifndef NONSTD_TILE_MAP_H
#define NONSTD_TILE_MAP_H
#include "nonstd.h"
#include "nonstd_glfw_opengl.h"
#include <cglm/cglm.h>

#ifdef __cplusplus
extern "C"
{
#endif

// name, index, size, type, gl_type, normalized,
#define X_ATTRIBUTES                          \
    X(pos, 0, 3, float, GL_FLOAT, GL_FALSE)   \
    X(color, 1, 3, float, GL_FLOAT, GL_FALSE) \
    X(uv, 2, 2, float, GL_FLOAT, GL_FALSE)

    typedef struct map_vertex_s map_vertex_t;
    struct map_vertex_s
    {
#define X(NAME, INDEX, SIZE, TYPE, TYPE_GL, NORMALIZED) TYPE NAME[SIZE];
        X_ATTRIBUTES
#undef X
    };

#define MAP_DRAW(M) M.draw(&(M))
#define MAP_RELOAD(M, T, F) M.reload(&(M), T, F)
#define MAP_POP_LOADED(M, S) M.pop_loaded(&(M), S)
#define MAP_MOVE(M, X, Y, Z) M.move(&(M), X, Y, Z)

    enum tile_state_e
    {
        UNLOADED,
        LOADED,
        INLOAD_QUEUE,
        INLOADED_QUEUE
    };
    typedef struct ellipsoid ellipsoid_t;
    typedef struct Coordinate_Operation_Parameter Coordinate_Operation_Parameter_t;
    typedef struct tile_s tile_t;
    typedef struct map_s map_t;
    typedef enum tile_state_e tile_state_t;
    typedef int (*map_draw_func_t)(map_t *map);
    typedef int (*map_reload_func_t)(map_t *map, tile_t *tile);
    typedef int (*map_push_load_func_t)(map_t *map, tile_t *);
    typedef int (*map_pop_loaded_func_t)(map_t *map, int *stop);
    typedef int (*map_move_func_t)(map_t *map, float x, float y, float z);
    typedef void (*async_load_func_t)(void *);

    struct tile_s
    {
        pthread_mutex_t tile_mutex;
        map_t *map;
        int x_index;
        int y_index;
        int z_index;
        int state;
        int width;
        int height;
        int channels;
        unsigned char *data;
    };
    struct ellipsoid
    {
        union
        {
            float semi_major_axis;
            float a;
        };
        union
        {
            float semi_minor_axis;
            float b;
        };
    };
    struct Coordinate_Point
    {
        union
        {
            float lon;
            float E;
        };
        union
        {
            float lat;
            float N;
        };
    };
    struct Coordinate_Operation_Parameter
    {
        int type;
        union
        {
            float p1;
            float lat_O; // Latitude_natural_origin
            float lat_C; // Latitude_projection_centre
            float lat_1; // Latitude_1st_standard_parallel
        };
        union
        {
            float p2;
            float lon_O; // Longitude of natural origin
            float lon_C; // Longitude of projection centre
            float lat_2; // Latitude of 2nd standard parallel
        };
        union
        {
            float p3;
            float K_O; // Scale factor at natural origin
            float K_C; // Scale factor on initial line
        };
        union
        {
            float p4;
            float FE;  // False easting
            float E_F; // Easting at false origin
            float E_C; // Easting at projection centre
        };
        union
        {
            float p5;
            float FN;  // False northing
            float N_F; // Northing at false origin
            float N_C; // Northing at projection centre
        };
        union
        {
            float p6;
            float lat_F; // Latitude of false origin
            float A_C;   // Azimuth of initial line
        };
        union
        {
            float p7;
            float lon_F; // Latitude of false origin
            float Y_C;   // Angle from Rectified to Skewed grid
        };
    };
    struct map_s
    {
        const char *root_dir;
        ellipsoid_t source_Ellipsoid;
        ellipsoid_t target_Ellipsoid;
        Coordinate_Operation_Parameter_t source_projection;
        Coordinate_Operation_Parameter_t target_projection;
        nonstd_opengl_ubo_t uboSourceEllipsoid;
        nonstd_opengl_ubo_t uboTargetEllipsoid;
        nonstd_opengl_ubo_t uboSourceProjection;
        nonstd_opengl_ubo_t uboTargetProjection;

        tile_t *tile_array;
        shader_t *shader;
        unsigned int VAO;
        unsigned int VBO;

        texture_t map_texture;

        task_queue_t *tq;
        queue_t loaded_queue;

        map_draw_func_t draw;
        map_reload_func_t reload;
        map_push_load_func_t push_load;
        map_pop_loaded_func_t pop_loaded;
        map_move_func_t move;
        async_load_func_t async_load;
    };

    int init_map(
        map_t *map,
        const char *root_dir,
        size_t tile_array_length,
        tile_t *tile_array,
        task_queue_t *tq,
        size_t loaded_queue_buffer_length,
        void *loaded_queue_buffer,
        shader_t *shader,
        push_func_t loaded_push_func,
        pop_func_t loaded_pop_func,
        map_draw_func_t draw,
        map_reload_func_t reload,
        map_push_load_func_t push_load,
        map_pop_loaded_func_t pop_loaded,
        map_move_func_t move,
        async_load_func_t async_load);
#ifdef __cplusplus
} /* extern "C" */
#endif

#endif