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
#define MAP_POP_LOADED(M) M.pop_loaded(&(M))

    typedef struct map_s map_t;
    typedef struct tile_s tile_t;
    typedef enum tile_state_e tile_state_t;
    typedef int (*map_draw_func_t)(map_t *map);
    typedef int (*map_reload_func_t)(map_t *map, tile_t *tile, const char *filename);
    typedef int (*map_push_load_func_t)(map_t *map, tile_t *);
    typedef int (*map_pop_loaded_func_t)(map_t *map);
    typedef void (*async_load_func_t)(void *);

    struct tile_s
    {
        pthread_mutex_t tile_mutex;
        map_t *map;
        int xoffset;
        int yoffset;
        int zoffset;
        int state;
        char filename[1024];
        int width;
        int height;
        int channels;
        unsigned char *data;
    };
    struct map_s
    {
        shader_t *shader;
        tile_t *tile_array;
        unsigned int VAO;
        unsigned int VBO;

        texture_t map_texture;

        task_queue_t *tq;
        queue_t loaded_queue;

        map_draw_func_t draw;
        map_reload_func_t reload;
        map_push_load_func_t push_load;
        map_pop_loaded_func_t pop_loaded;
        async_load_func_t async_load;
    };
    enum tile_state_e
    {
        UNLOADED,
        LOADED,
        INLOAD_QUEUE,
        INLOADED_QUEUE
    };

    int init_map(
        map_t *map,
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
        async_load_func_t async_load);
#ifdef __cplusplus
} /* extern "C" */
#endif

#endif