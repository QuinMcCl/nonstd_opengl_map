
#include <stb/stb_image.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "nonstd.h"
#include "tile_map.h"

#define ON_ERROR return errno;
const map_vertex_t vertices[] = {
    // positions          // colors           // texture coords
    {{-1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 0.0f}, {0.0f, 1.0f}},  // top left
    {{-1.0f, -1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}}, // bottom left
    {{1.0f, 1.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},   // top right
    {{1.0f, -1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},  // bottom right
};
int __map_draw(map_t *map)
{
    CHECK_ERR(shader_use(map->shader));

    GLint prevTextureUnit = GL_TEXTURE0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevTextureUnit);
    glActiveTexture(GL_TEXTURE0 + map->map_texture.unit);

    GLint prevTextureID = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTextureID);
    glBindTexture(GL_TEXTURE_2D, map->map_texture.ID);

    CHECK_ERR(shader_set(map->shader, "MAP_TEXTURE", I1, 1, &map->map_texture.unit));
    glBindVertexArray(map->VAO);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, prevTextureID);
    glActiveTexture(prevTextureUnit);
    return 0;
}

#undef ON_ERROR
#define ON_ERROR return;

void __map_async_load(void *args)
{
    assert(args);
    tile_t *tile = args;
    CHECK_ERR(pthread_mutex_lock(&(tile->tile_mutex)));
    if (tile->data != NULL)
    {
        stbi_image_free(tile->data);
        tile->data = NULL;
    }

    stbi_set_flip_vertically_on_load(1);
    char filename[1024] = {0};
    snprintf(filename, 1024, "%s/%d/%d/%d.png", tile->map->root_dir, tile->z_index, tile->x_index, tile->y_index);
    tile->data = stbi_load(filename, &(tile->width), &(tile->height), &(tile->channels), 0);
    if (tile->data != NULL)
    {
        if (!QUEUE_PUSH(tile->map->loaded_queue, tile, 1))
        {
            tile->state = INLOADED_QUEUE;
        }
    }
    else
    {
        tile->state = UNLOADED;
    }
    CHECK_ERR(pthread_mutex_unlock(&(tile->tile_mutex)));
}

#undef ON_ERROR
#define ON_ERROR return errno;

int __map_pop_loaded_queue(map_t *map, int *stop)
{
    tile_t *tile = NULL;
    CHECK_ERR(QUEUE_POP(map->loaded_queue, tile, 0));
    if (tile == NULL)
    {
        *stop = 1;
        return 0;
    }

    CHECK_ERR(pthread_mutex_lock(&(tile->tile_mutex)));
    if (tile->state == INLOADED_QUEUE)
    {
        GLenum format = GL_FALSE;
        if (tile->channels == 1)
            format = GL_RED;
        else if (tile->channels == 3)
            format = GL_RGB;
        else if (tile->channels == 4)
            format = GL_RGBA;

        GLint prevTextureUnit = GL_TEXTURE0;
        glGetIntegerv(GL_ACTIVE_TEXTURE, &prevTextureUnit);
        glActiveTexture(GL_TEXTURE0 + map->map_texture.unit);

        GLint prevTextureID = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTextureID);
        glBindTexture(GL_TEXTURE_2D, map->map_texture.ID);

        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
        glTexSubImage2D(GL_TEXTURE_2D, 0, tile->x_index * tile->width, tile->y_index * tile->height, tile->width, tile->height, format, GL_UNSIGNED_BYTE, tile->data);

        glBindTexture(GL_TEXTURE_2D, prevTextureID);
        glActiveTexture(prevTextureUnit);
        tile->state = LOADED;
    }
    CHECK_ERR(pthread_mutex_unlock(&(tile->tile_mutex)));
    return 0;
}

int __map_push_load_queue(map_t *map, tile_t *tile)
{
    int retval = 0;
    async_task_t task = {0};
    assert(tile->state == UNLOADED);
    task.funcName = "async_load";
    task.func = map->async_load;
    task.args = tile;
    tile->state = INLOAD_QUEUE;
    retval = QUEUE_PUSH(map->tq->queue, task, 0);
    if (retval)
    {
        tile->state = UNLOADED;
    }
    return retval;
}

int __map_reload(map_t *map, tile_t *tile)
{
    int retval = 0;
    CHECK_ERR(pthread_mutex_lock(&(tile->tile_mutex)));
    if (tile->state != INLOAD_QUEUE)
    {
        tile->state = UNLOADED;
        retval = map->push_load(map, tile);
    }
    CHECK_ERR(pthread_mutex_unlock(&(tile->tile_mutex)));
    return retval;
}

#define DEFAULT_MAP_DRAW __map_draw
#define DEFAULT_MAP_RELOAD __map_reload
#define DEFAULT_MAP_PUSH_LOAD __map_push_load_queue
#define DEFAULT_MAP_POP_LOADED __map_pop_loaded_queue
#define DEFAULT_MAP_ASYNC_LOAD __map_async_load

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
    async_load_func_t async_load)
{
    map->root_dir = root_dir;

    unsigned int VAO;
    unsigned int VBO;

    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);

    glBindVertexArray(VAO);

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

#define X(NAME, INDEX, SIZE, TYPE, TYPE_GL, NORMALIZED)                                                                  \
    glVertexAttribPointer(INDEX, SIZE, TYPE_GL, NORMALIZED, sizeof(map_vertex_t), (void *)offsetof(map_vertex_t, NAME)); \
    glEnableVertexAttribArray(INDEX);
    X_ATTRIBUTES
#undef X

    CHECK_ERR(pthread_mutex_lock(&(map->map_texture.mutex_lock)));

    // map->map_texture.width = 8192;
    // map->map_texture.height = 8192;
    // TODO FIX?
    map->map_texture.width = 256 * 16;
    map->map_texture.height = 256 * 16;
    map->map_texture.channels = 4;
    map->map_texture.ID = GL_FALSE;
    map->map_texture.unit = -1;

    glGenTextures(1, &(map->map_texture.ID));

    GLint prevTextureUnit = GL_TEXTURE0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevTextureUnit);
    glActiveTexture(GL_TEXTURE0);

    GLint prevTextureID = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTextureID);

    glBindTexture(GL_TEXTURE_2D, map->map_texture.ID);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, map->map_texture.width, map->map_texture.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    // glGenerateMipmap(GL_TEXTURE_2D);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindTexture(GL_TEXTURE_2D, prevTextureID);
    glActiveTexture(prevTextureUnit);

    CHECK_ERR(pthread_mutex_unlock(&(map->map_texture.mutex_lock)));

    CHECK_ERR(queue_init(&(map->loaded_queue), loaded_queue_buffer_length, loaded_queue_buffer, sizeof(tile_t *), 8UL, loaded_push_func, loaded_pop_func));

    map->tq = tq;
    map->shader = shader;
    map->VAO = VAO;
    map->VBO = VBO;

    map->tile_array = tile_array;

    map->draw = draw == NULL ? DEFAULT_MAP_DRAW : draw;
    map->reload = reload == NULL ? DEFAULT_MAP_RELOAD : reload;
    map->push_load = push_load == NULL ? DEFAULT_MAP_PUSH_LOAD : push_load;
    map->pop_loaded = pop_loaded == NULL ? DEFAULT_MAP_POP_LOADED : pop_loaded;
    map->async_load = async_load == NULL ? DEFAULT_MAP_ASYNC_LOAD : async_load;

    for (size_t tile_index = 0; tile_index < tile_array_length; tile_index++)
    {
        map->tile_array[tile_index].map = map;
        // TODO FIX?
        map->tile_array[tile_index].x_index = (tile_index / 16);
        map->tile_array[tile_index].y_index = (tile_index % 16);
        map->tile_array[tile_index].z_index = 4;
        map->tile_array[tile_index].state = UNLOADED;
        map->push_load(map, &(map->tile_array[tile_index]));
    }

    return 0;
}

#undef ON_ERROR