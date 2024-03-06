
#include <stb/stb_image.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "nonstd.h"
#include "tile_map.h"

const map_vertex_t vertices[] = {
    // positions          // colors           // texture coords
    {{-1.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, {0.0f, 1.0f}},  // top left
    {{-1.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}}, // bottom left
    {{1.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},   // top right
    {{1.0f, -1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},  // bottom right
};
int __map_draw(map_t *map)
{
    GLint prevTextureUnit;
    long int unit = 0l;
    CHECK_ERR(shader_use(map->shader), strerror(errno), return errno);
    // CHECK_ERR(texture_activate(map->mTexturePtr, &unit), strerror(errno), return errno);

    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevTextureUnit);

    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, map->TID);
    CHECK_ERR(shader_set(map->shader, "MAP_TEXTURE", I1, 1, &unit), strerror(errno), return errno);
    glBindVertexArray(map->VAO);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    // glBindVertexArray(0);
    // glActiveTexture(prevTextureUnit);
    return 0;
}

void __map_async_load(void *args)
{
    assert(args);
    tile_t *tile = args;
    if (tile->data != NULL)
    {
        stbi_image_free(tile->data);
    }

    tile->data = stbi_load(tile->filename, &(tile->width), &(tile->height), &(tile->channels), 0);
    if (tile->data != NULL)
    {
        QUEUE_PUSH(tile->map->loaded_queue, tile, 1);
    }
}

int __map_pop_loaded_queue(map_t *map)
{
    int retval = 0;
    tile_t *tile = NULL;
    retval = QUEUE_POP(map->loaded_queue, tile, 0);
    if (retval)
        return retval;
    if (tile == NULL)
        return 0;
    GLint prevTexture2D;
    GLenum format = GL_FALSE;
    if (tile->channels == 1)
        format = GL_RED;
    else if (tile->channels == 3)
        format = GL_RGB;
    else if (tile->channels == 4)
        format = GL_RGBA;

    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTexture2D);

    glBindTexture(GL_TEXTURE_2D, map->TID);
    glTexImage2D(GL_TEXTURE_2D, 0, format, tile->width, tile->height, 0, format, GL_UNSIGNED_BYTE, tile->data);

    glGenerateMipmap(GL_TEXTURE_2D);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindTexture(GL_TEXTURE_2D, prevTexture2D);
    tile->state = LOADED;
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

int __map_reload(map_t *map, const char *filename)
{
    snprintf(map->tile.filename, 1024, "%s", filename);
    map->tile.state = UNLOADED;
    return map->push_load(map, &(map->tile));
}

#define DEFAULT_MAP_DRAW __map_draw
#define DEFAULT_MAP_RELOAD __map_reload
#define DEFAULT_MAP_PUSH_LOAD __map_push_load_queue
#define DEFAULT_MAP_POP_LOADED __map_pop_loaded_queue
#define DEFAULT_MAP_ASYNC_LOAD __map_async_load

int init_map(
    map_t *map,
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

    glGenTextures(1, &(map->TID));

    CHECK_ERR(queue_init(&(map->loaded_queue), loaded_queue_buffer_length, loaded_queue_buffer, sizeof(tile_t *), 8UL, loaded_push_func, loaded_pop_func), strerror(errno), return errno);

    map->tq = tq;
    map->shader = shader;
    map->VAO = VAO;
    map->VBO = VBO;

    map->tile.map = map;

    map->draw = draw == NULL ? DEFAULT_MAP_DRAW : draw;
    map->reload = reload == NULL ? DEFAULT_MAP_RELOAD : reload;
    map->push_load = push_load == NULL ? DEFAULT_MAP_PUSH_LOAD : push_load;
    map->pop_loaded = pop_loaded == NULL ? DEFAULT_MAP_POP_LOADED : pop_loaded;
    map->async_load = async_load == NULL ? DEFAULT_MAP_ASYNC_LOAD : async_load;
    return 0;
}