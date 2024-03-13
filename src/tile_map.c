
#include <stb/stb_image.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "nonstd.h"
#include "tile_map.h"

#define ON_ERROR return errno;

// #define GLM_E         2.71828182845904523536028747135266250   /* e           */
// #define GLM_LOG2E     1.44269504088896340735992468100189214   /* log2(e)     */
// #define GLM_LOG10E    0.434294481903251827651128918916605082  /* log10(e)    */
// #define GLM_LN2       0.693147180559945309417232121458176568  /* loge(2)     */
// #define GLM_LN10      2.30258509299404568401799145468436421   /* loge(10)    */
// #define GLM_PI        3.14159265358979323846264338327950288   /* pi          */
// #define GLM_PI_2      1.57079632679489661923132169163975144   /* pi/2        */
// #define GLM_PI_4      0.785398163397448309615660845819875721  /* pi/4        */

void web_merc_forward(struct Coordinate_Point *out, const struct ellipsoid E, const struct Coordinate_Operation_Parameter params, const struct Coordinate_Point in)
{
    out->E = params.FE + E.a * (in.lon - params.lon_O);
    out->N = params.FN + E.a * log(tan(GLM_PI_4 + in.lat / 2.0f));
}
void web_merc_reverse(struct Coordinate_Point *out, const struct ellipsoid E, const struct Coordinate_Operation_Parameter params, const struct Coordinate_Point in)
{
    out->lon = ((in.E - params.FE) / E.a) + params.lon_O;
    out->lat = GLM_PI_2 - 2.0f * atan(pow(GLM_E, (params.FN - in.N) / E.a));
}

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
    CHECK_ERR(nonstd_opengl_ubo_fill(&(map->uboSourceProjection), &(map->source_projection), sizeof(map->source_projection), 0));
    CHECK_ERR(nonstd_opengl_ubo_fill(&(map->uboTargetProjection), &(map->target_projection), sizeof(map->target_projection), 0));
    CHECK_ERR(nonstd_opengl_ubo_fill(&(map->uboSourceEllipsoid), &(map->source_Ellipsoid), sizeof(map->source_Ellipsoid), 0));
    CHECK_ERR(nonstd_opengl_ubo_fill(&(map->uboTargetEllipsoid), &(map->target_Ellipsoid), sizeof(map->target_Ellipsoid), 0));

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

int __map_move(map_t *map, float xOffset, float yOffset, float zOffset)
{
    switch (map->target_projection.type)
    {
    case 0:
        break;
    case 1:
        map->target_projection.FE += xOffset;
        map->target_projection.FN += yOffset;
        map->target_projection.K_O += zOffset;
        break;
    default:
        break;
    }
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
#define DEFAULT_MAP_MOVE __map_move
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
    map_move_func_t move,
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
    // TODO CLEAN THESE UP ON EXIT
    CHECK_ERR(shader_use(shader));
    CHECK_ERR(nonstd_opengl_ubo_init(&(map->uboSourceProjection), "uboSourceProjection", sizeof(map->source_projection), GL_STREAM_DRAW));
    CHECK_ERR(nonstd_opengl_ubo_init(&(map->uboTargetProjection), "uboTargetProjection", sizeof(map->target_projection), GL_STREAM_DRAW));
    CHECK_ERR(nonstd_opengl_ubo_init(&(map->uboSourceEllipsoid), "uboSourceEllipsoid", sizeof(map->source_Ellipsoid), GL_STREAM_DRAW));
    CHECK_ERR(nonstd_opengl_ubo_init(&(map->uboTargetEllipsoid), "uboTargetEllipsoid", sizeof(map->target_Ellipsoid), GL_STREAM_DRAW));

    CHECK_ERR(shader_bindBuffer(shader, map->uboSourceProjection.name, map->uboSourceProjection.bindingPoint));
    CHECK_ERR(shader_bindBuffer(shader, map->uboTargetProjection.name, map->uboTargetProjection.bindingPoint));
    CHECK_ERR(shader_bindBuffer(shader, map->uboSourceEllipsoid.name, map->uboSourceEllipsoid.bindingPoint));
    CHECK_ERR(shader_bindBuffer(shader, map->uboTargetEllipsoid.name, map->uboTargetEllipsoid.bindingPoint));

    CHECK_ERR(pthread_mutex_lock(&(map->map_texture.mutex_lock)));
    {
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
    }
    CHECK_ERR(pthread_mutex_unlock(&(map->map_texture.mutex_lock)));

    CHECK_ERR(queue_init(&(map->loaded_queue), loaded_queue_buffer_length, loaded_queue_buffer, sizeof(tile_t *), 8UL, loaded_push_func, loaded_pop_func));

    map->root_dir = root_dir;
    map->tq = tq;
    map->shader = shader;
    map->VAO = VAO;
    map->VBO = VBO;

    map->tile_array = tile_array;

    map->draw = draw == NULL ? DEFAULT_MAP_DRAW : draw;
    map->reload = reload == NULL ? DEFAULT_MAP_RELOAD : reload;
    map->push_load = push_load == NULL ? DEFAULT_MAP_PUSH_LOAD : push_load;
    map->pop_loaded = pop_loaded == NULL ? DEFAULT_MAP_POP_LOADED : pop_loaded;
    map->move = move == NULL ? DEFAULT_MAP_MOVE : move;
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