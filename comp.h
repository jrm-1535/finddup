
#ifndef __COMP_H__
#define __COMP_H__

#include <stdlib.h>
#include <stdbool.h>
#include "map.h"

typedef struct {
    char        *path;
    bool        nosub, zero;
} search_t;

typedef struct {
    search_t    *paths;
    search_t    *target;
    bool        absent, compare, remove, confirm;
} args_t;

static inline void error( char *msg )
{
    printf( "%s\n", msg );
    exit(1);
}

static inline void set_new_target( args_t *args,
                                   char *path, bool nosub, bool zero )
{
    search_t *target = malloc( sizeof(search_t));
    if ( NULL == target ) {
        error( "out of memory for target" );
    }
    target->path = path;
    target->nosub = nosub;
    target->zero = zero;
    args->target = target;
}

static inline search_t *new_paths( int n_paths )
{
    search_t *paths = malloc( sizeof(search_t) * n_paths );
    if ( NULL == paths ) {
        error( "out of memory for paths" );
    }
    memset( paths, 0, sizeof(search_t) * n_paths  );
    return paths;
}

static inline void set_path( args_t *args, int index,
                             char *path, bool nosub, bool zero )
{
    args->paths[index].path = path;
    args->paths[index].nosub = nosub;
    args->paths[index].zero = zero;
}

static inline void free_target_n_paths( args_t *args )
{
    free( args->target );
    free( args->paths );
}

extern map_t *collect_same_size_files( args_t *args );
extern void process_same_size_files( map_t *map, args_t *args );
extern void free_collected_data( map_t *map );

#endif /* __COMP_H__ */
