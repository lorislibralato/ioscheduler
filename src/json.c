#include <stdio.h>
#include <stdlib.h>

typedef struct json_object
{
    enum json_type
    {
        JSON_OBJECT,
        JSON_ARRAY,
        JSON_STRING,
        JSON_NUMBER
    } type;
    union
    {
        JsonArray *json_array;
        struct json_object *json_object;
        char *string;
        float number;
    };
} JsonObject;

typedef struct json_array
{
    union
    {
        JsonObject *json_array;
    };
} JsonArray;

#define OTHER_OBJECT_PARAM __others_parameters
#define OTHER_OBJECT JsonObject *OTHER_OBJECT_PARAM
#define OFFSET_OF(type, member) ((size_t)&((type *)0)->member)
#define STRINGIFY(type, member) #member

typedef struct film_response
{
    char *name;
    float year;

    OTHER_OBJECT;
} FilmResponse;

typedef struct author_response
{
    char *name;
    float year;

    OTHER_OBJECT;
} AuthorResponse;

#define KEYS(type, ...) \
    type *__ANON;       \
    static char *type##_KEYS = {__VA_ARGS__}

#define VALUES(type, ...) \
    type *__ANON;       \
    static char *type##_KEYS = {__VA_ARGS__}

#define ATTRIBUTE(member) STRINGIFY(typeof(__ANON), member)

KEYS(FilmResponse,
     ATTRIBUTE(name),
     ATTRIBUTE(year));

KEYS(AuthorResponse,
     ATTRIBUTE(name),
     ATTRIBUTE(year));

static size_t *values = {
    OFFSET_OF(FilmResponse, name),
    OFFSET_OF(FilmResponse, year),
    OFFSET_OF(FilmResponse, OTHER_OBJECT_PARAM),
};

void test()
{
    char buf[] = "{\"name\": \"The Godfather\", \"year\": 1972}";

    FilmResponse film;
    film.name = &buf[14];
    film.year = 1972;
    film.OTHER_OBJECT_PARAM = NULL;
}
